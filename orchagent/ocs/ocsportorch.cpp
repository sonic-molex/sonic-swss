#include "ocsportorch.h"
#include "sai_serialize.h"
#include "sai_serialize_ext.h"
#include "schema.h"


extern sai_ocs_port_api_t *sai_ocs_port_api;


#define OCS_PORT_NOTIFICATION                           "OCS_PORT_NOTIFICATION"
#define OCS_PORT_REPLY                                  "OCS_PORT_REPLY"
#define OCS_PORT_ATTR_FLEX_COUNTER_GROUP                "OCS_PORT_ATTR_COUNTER"
#define OCS_PORT_COUNTER_DEFAULT_POLLING_INTERVAL_MS    1000 // ms
#define OCS_PORT_COUNTER_DEFAULT_ENABLED_STATE          true

OcsPortOrch::OcsPortOrch(DBConnector *db, const std::vector<std::string> &table_names) :
    ObjectOrch(db, table_names,
    (sai_object_type_t)SAI_OBJECT_TYPE_OCS_PORT,
    CounterType::OCS_PORT_STATS)
{
    SWSS_LOG_ENTER();

    std::string scriptPath = "ocs_port_attrs.lua";
    loadFlexCounterScript(scriptPath,
                          OCS_PORT_ATTRS_PLUGIN_FIELD,
                          OCS_PORT_ATTR_FLEX_COUNTER_GROUP,
                          StatsMode::READ,
                          OCS_PORT_COUNTER_DEFAULT_POLLING_INTERVAL_MS,
                          OCS_PORT_COUNTER_DEFAULT_ENABLED_STATE);

    m_stateTable = std::unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OCS_PORT_TABLE_NAME));
    m_nameMapTable = std::unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OCS_PORT_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OCS_PORT_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OCS_PORT_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OCS_PORT_REPLY);

    if (sai_ocs_port_api == NULL)
    {
        SWSS_LOG_ERROR("OcsPortOrch: FATAL - sai_ocs_port_api is NULL!");
        return;
    }
    m_createFunc = sai_ocs_port_api->create_ocs_port;
    m_removeFunc = sai_ocs_port_api->remove_ocs_port;
    m_setFunc = sai_ocs_port_api->set_ocs_port_attribute;
    m_getFunc = sai_ocs_port_api->get_ocs_port_attribute;
}

void OcsPortOrch::setFlexCounter(sai_object_id_t id)
{
    SWSS_LOG_ENTER();

    /* add stats */
    std::unordered_set<std::string> counter_stats;
    for (const auto& it : m_statValues) {
        counter_stats.emplace(it.first);
    }
    m_flex_stat_manager->setCounterIdList(id, CounterType::OCS_PORT_STATS, counter_stats);

    /* add attrs */
    std::unordered_set<std::string> counter_attrs;
    for (const auto& it : m_readonlyOrgAttrs) {
        counter_attrs.emplace(it.first);
    }
    m_flex_stat_manager->setCounterIdList(id, CounterType::OCS_PORT_ATTRS, counter_attrs);
}

void OcsPortOrch::addExtraAttrsOnCreate(const std::string &key, std::vector<sai_attribute_t> &attrs)
{
    SWSS_LOG_ENTER();
    SWSS_LOG_NOTICE("Add extra attributes, port id: %s", key.c_str());

    // Add key to port id.
    sai_attribute_t attr;
    attr.id = SAI_OCS_PORT_ATTR_SIMPLEX_PORT_ID;
    strncpy(attr.value.chardata, key.c_str(), sizeof(attr.value.chardata));
    attrs.push_back(attr);
}
