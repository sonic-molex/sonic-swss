#include <chrono>
#include <inttypes.h>
#include "ocscrossconnectorch.h"
#include "sai_serialize.h"
#include "sai_serialize_ext.h"
#include "schema.h"
#include "redispipeline.h"


extern sai_ocs_cross_connect_api_t *sai_ocs_cross_connect_api;

#define OCS_CROSS_CONNECT_NOTIFICATION                         "OCS_CROSS_CONNECT_NOTIFICATION"
#define OCS_CROSS_CONNECT_REPLY                                "OCS_CROSS_CONNECT_REPLY"
#define OCS_CROSS_CONNECT_ATTR_FLEX_COUNTER_GROUP              "OCS_CROSS_CONNECT_ATTR_COUNTER"
#define OCS_CROSS_CONNECT_COUNTER_DEFAULT_POLLING_INTERVAL_MS  1000 // ms
#define OCS_CROSS_CONNECT_COUNTER_DEFAULT_ENABLED_STATE        true
#define OCS_CROSS_CONNECT_POP_BATCH_SIZE                       65536

OcsCrossConnectOrch::OcsCrossConnectOrch(DBConnector *applDb,
                                         const std::vector<std::string> &table_names) :
    ObjectOrch(applDb, table_names,
    (sai_object_type_t)SAI_OBJECT_TYPE_OCS_CROSS_CONNECT,
    CounterType::OCS_CROSS_CONNECT_ATTRS)
{
    SWSS_LOG_ENTER();

    if (table_names.empty())
    {
        addConsumer(applDb, APP_OCS_CROSS_CONNECT_TABLE_NAME, default_orch_pri, OCS_CROSS_CONNECT_POP_BATCH_SIZE);
    }

    std::string scriptPath = "ocs_cross_connect_attrs.lua";
    loadFlexCounterScript(scriptPath,
                          OCS_CROSS_CONNECT_ATTRS_PLUGIN_FIELD,
                          OCS_CROSS_CONNECT_ATTR_FLEX_COUNTER_GROUP,
                          StatsMode::READ,
                          OCS_CROSS_CONNECT_COUNTER_DEFAULT_POLLING_INTERVAL_MS,
                          OCS_CROSS_CONNECT_COUNTER_DEFAULT_ENABLED_STATE);

    m_stateTable = std::unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OCS_CROSS_CONNECT_TABLE_NAME));
    m_nameMapTable = std::unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OCS_CROSS_CONNECT_NAME_MAP));

    m_cfgDb = std::shared_ptr<DBConnector>(new DBConnector("CONFIG_DB", 0));
    m_cfgDbTable = std::unique_ptr<Table>(new Table(m_cfgDb.get(), CFG_OCS_CROSS_CONNECT_TABLE_NAME));

    m_notificationConsumer = new NotificationConsumer(applDb, OCS_CROSS_CONNECT_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OCS_CROSS_CONNECT_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(applDb, OCS_CROSS_CONNECT_REPLY);

    if (sai_ocs_cross_connect_api == NULL)
    {
        SWSS_LOG_ERROR("OcsCrossConnectOrch: FATAL - sai_ocs_cross_connect_api is NULL!");
        return;
    }
    m_createFunc = sai_ocs_cross_connect_api->create_ocs_cross_connect;
    m_removeFunc = sai_ocs_cross_connect_api->remove_ocs_cross_connect;
    m_setFunc = sai_ocs_cross_connect_api->set_ocs_cross_connect_attribute;
    m_getFunc = sai_ocs_cross_connect_api->get_ocs_cross_connect_attribute;
    m_bulkCreateFunc = sai_ocs_cross_connect_api->create_ocs_cross_connects;
    m_bulkRemoveFunc = sai_ocs_cross_connect_api->remove_ocs_cross_connects;
}

void OcsCrossConnectOrch::setFlexCounter(sai_object_id_t id)
{
    SWSS_LOG_ENTER();

    /* add attrs */
    std::unordered_set<std::string> counter_attrs;
    for (const auto& it : m_readonlyOrgAttrs) {
        counter_attrs.emplace(it.first);
    }
    m_flex_stat_manager->setCounterIdList(id, CounterType::OCS_CROSS_CONNECT_ATTRS, counter_attrs);
}

void OcsCrossConnectOrch::addExtraAttrsOnCreate(const std::string &key, std::vector<sai_attribute_t> &attrs)
{
    SWSS_LOG_ENTER();
    SWSS_LOG_NOTICE("Add extra attributes, cross connect id: %s", key.c_str());

    sai_attribute_t attr;
    attr.id = SAI_OCS_CROSS_CONNECT_ATTR_CROSS_CONNECT_ID;
    strncpy(attr.value.chardata, key.c_str(), sizeof(attr.value.chardata));
    attrs.push_back(attr);
}

std::vector<std::string> OcsCrossConnectOrch::collectInvalidConnections()
{
    SWSS_LOG_ENTER();

    std::vector<std::string> invalidKeys;

    for (const auto &entry : m_key2oid)
    {
        sai_attribute_t attr;
        attr.id = SAI_OCS_CROSS_CONNECT_ATTR_OPER_STATUS;

        sai_status_t status = m_getFunc(entry.second, 1, &attr);
        if (status != SAI_STATUS_SUCCESS)
        {
            SWSS_LOG_WARN("collectInvalid: failed to get oper_status for %s oid:%" PRIx64 ", rv=%d",
                          entry.first.c_str(), entry.second, status);
            continue;
        }

        if (attr.value.s32 == SAI_OCS_CROSS_CONNECT_OPER_STATUS_INVALID)
        {
            invalidKeys.push_back(entry.first);
        }
    }

    if (!invalidKeys.empty())
    {
        SWSS_LOG_NOTICE("collectInvalid: found %zu INVALID connections out of %zu total",
                        invalidKeys.size(), m_key2oid.size());
    }

    return invalidKeys;
}

void OcsCrossConnectOrch::cleanupInvalidConnections(const std::vector<std::string> &invalidKeys)
{
    SWSS_LOG_ENTER();

    auto t0 = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    SWSS_LOG_NOTICE("autoClean: removing %zu INVALID connections from SAI, orchagent, and CONFIG_DB",
                    invalidKeys.size());

    if (!bulkRemoveObjects(invalidKeys))
    {
        SWSS_LOG_ERROR("autoClean: bulkRemoveObjects failed for INVALID connections");
    }

    RedisPipeline cfgPipe(m_cfgDb.get(), 256);
    Table cfgBatch(&cfgPipe, m_cfgDbTable->getTableName(), false);

    for (const auto &key : invalidKeys)
    {
        cfgBatch.del(key);
    }
    cfgPipe.flush();

    auto t1 = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    SWSS_LOG_NOTICE("autoClean: completed, %zu keys removed, elapsed=%lldms",
                    invalidKeys.size(), (long long)(t1 - t0));
}

