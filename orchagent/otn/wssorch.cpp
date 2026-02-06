#include "wssorch.h"
#include "schema.h"
#include <saiexperimentalotnwss.h>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <algorithm>

extern sai_otn_wss_api_t *sai_otn_wss_api;

#define OTN_WSS_NOTIFICATION                         "OTN_WSS_NOTIFICATION"
#define OTN_WSS_REPLY                                "OTN_WSS_REPLY"
#define OTN_WSS_FLEX_COUNTER_GROUP                   "OTN_WSS_FLEX_COUNTER"
#define OTN_WSS_DEFAULT_POLLING_INTERVAL_MS          1000 // ms
#define OTN_WSS_DEFAULT_ENABLED_STATE                true

#define OTN_WSS_SPEC_POWER_NOTIFICATION             "OTN_WSS_SPEC_POWER_NOTIFICATION"
#define OTN_WSS_SPEC_POWER_REPLY                     "OTN_WSS_SPEC_POWER_REPLY"
#define OTN_WSS_SPEC_POWER_FLEX_COUNTER_GROUP       "OTN_WSS_SPEC_POWER_FLEX_COUNTER"
#define OTN_WSS_SPEC_POWER_DEFAULT_POLLING_INTERVAL_MS  1000 // ms
#define OTN_WSS_SPEC_POWER_DEFAULT_ENABLED_STATE    true

WssOrch::WssOrch(DBConnector *db, const std::vector<std::string> &table_names) :
    ObjectOrch(db, table_names, (sai_object_type_t)SAI_OBJECT_TYPE_OTN_WSS, CounterType::OTN_WSS_ATTR)
{
    SWSS_LOG_ENTER();

    createFlexCounter("",
                      OTN_WSS_PLUGIN_FIELD,
                      OTN_WSS_FLEX_COUNTER_GROUP,
                      StatsMode::READ,
                      OTN_WSS_DEFAULT_POLLING_INTERVAL_MS,
                      OTN_WSS_DEFAULT_ENABLED_STATE);

    m_stateTable = std::unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OTN_WSS_TABLE_NAME));
    m_nameMapTable = std::unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OTN_WSS_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OTN_WSS_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OTN_WSS_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OTN_WSS_REPLY);

    m_createFunc = sai_otn_wss_api->create_otn_wss;
    m_removeFunc = sai_otn_wss_api->remove_otn_wss;
    m_setFunc = sai_otn_wss_api->set_otn_wss_attribute;
    m_getFunc = sai_otn_wss_api->get_otn_wss_attribute;

}

void WssOrch::addExtraAttrsOnCreate(const std::string &key, std::vector<sai_attribute_t> &attrs)
{
    for (const auto &a : attrs)
    {
        if (a.id == SAI_OTN_WSS_ATTR_INDEX)
        {
            return;
        }
    }
    const char *p = key.c_str();
    while (*p && std::isdigit(static_cast<unsigned char>(*p)))
    {
        ++p;
    }
    if (p == key.c_str())
    {
        return;
    }
    uint32_t idx = static_cast<uint32_t>(std::strtoul(key.c_str(), nullptr, 10));
    sai_attribute_t attr;
    attr.id = SAI_OTN_WSS_ATTR_INDEX;
    attr.value.u32 = idx;
    attrs.insert(attrs.begin(), attr);
}

void WssSpecPowerOrch::addExtraAttrsOnCreate(const std::string &key, std::vector<sai_attribute_t> &attrs)
{
    /* Key format: "index|lower-frequency".
     *
     * lower-frequency is a key component in the sonic YANG, so CVL forbids
     * storing it as a hash field.  It won't appear in CONFIG_DB/APP_DB when
     * coming through gNMI, but SAI requires it as a mandatory create-only
     * attribute.
    */
    bool hasLowerFreq = false;
    bool hasSourcePort = false;
    for (const auto &a : attrs)
    {
        if (a.id == SAI_OTN_WSS_SPEC_POWER_ATTR_LOWER_FREQUENCY)
            hasLowerFreq = true;
        if (a.id == SAI_OTN_WSS_SPEC_POWER_ATTR_SOURCE_PORT_NAME)
            hasSourcePort = true;
    }

    // --- lower-frequency (from composite key) ---
    if (!hasLowerFreq)
    {
        auto sep = key.find('|');
        if (sep != std::string::npos)
        {
            std::string lowerStr = key.substr(sep + 1);
            uint64_t lowerFreq = std::strtoull(lowerStr.c_str(), nullptr, 10);

            // SAI attribute
            sai_attribute_t attr;
            attr.id = SAI_OTN_WSS_SPEC_POWER_ATTR_LOWER_FREQUENCY;
            attr.value.u64 = lowerFreq;
            attrs.insert(attrs.begin(), attr);

        }
    }

    // --- source-port-name: read from parent OTN_WSS in CONFIG_DB ---
    // CONFIG_DB is guaranteed to have the parent entry because translib writes
    // both OTN_WSS and OTN_WSS_SPEC_POWER atomically before either reaches orchagent.
    if (!hasSourcePort)
    {
        std::string port_name;
        auto sep = key.find('|');
        std::string parentIndex = (sep != std::string::npos) ? key.substr(0, sep) : key;

        std::vector<FieldValueTuple> fvs;
        if (m_parentWssCfgTable && m_parentWssCfgTable->get(parentIndex, fvs))
        {
            for (const auto &fv : fvs)
            {
                if (fvField(fv) == "source-port-name")
                {
                    port_name = fvValue(fv);
                    break;
                }
            }
        }

        if (port_name.empty())
        {
            SWSS_LOG_WARN("WSS spec power create: key %s has no source-port-name from parent OTN_WSS (index %s)",
                          key.c_str(), parentIndex.c_str());
            return;
        }
        sai_attribute_t attr;
        attr.id = SAI_OTN_WSS_SPEC_POWER_ATTR_SOURCE_PORT_NAME;
        size_t len = std::min(port_name.size(), sizeof(attr.value.chardata) - 1);
        std::memcpy(attr.value.chardata, port_name.c_str(), len);
        attr.value.chardata[len] = '\0';
        attrs.insert(attrs.begin(), attr);
    }
}

WssSpecPowerOrch::WssSpecPowerOrch(DBConnector *db, const std::vector<std::string> &table_names) :
    ObjectOrch(db, table_names, (sai_object_type_t)SAI_OBJECT_TYPE_OTN_WSS_SPEC_POWER, CounterType::OTN_WSS_SPEC_POWER_ATTR)
{
    SWSS_LOG_ENTER();

    std::string scriptPath = "otn_wss_pluggin.lua";
    createFlexCounter(scriptPath,
                      OTN_WSS_SPEC_POWER_PLUGIN_FIELD,
                      OTN_WSS_SPEC_POWER_FLEX_COUNTER_GROUP,
                      StatsMode::READ,
                      OTN_WSS_SPEC_POWER_DEFAULT_POLLING_INTERVAL_MS,
                      OTN_WSS_SPEC_POWER_DEFAULT_ENABLED_STATE);

    m_stateTable = std::unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OTN_WSS_SPEC_POWER_TABLE_NAME));
    m_nameMapTable = std::unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OTN_WSS_SPEC_POWER_NAME_MAP));

    // Parent WSS table in CONFIG_DB for looking up source-port-name during create.
    // CONFIG_DB is guaranteed to have the parent entry by the time this orch runs.
    m_cfgDb = std::make_shared<DBConnector>("CONFIG_DB", 0);
    m_parentWssCfgTable = std::unique_ptr<Table>(new Table(m_cfgDb.get(), CFG_OTN_WSS_TABLE_NAME));

    m_notificationConsumer = new NotificationConsumer(db, OTN_WSS_SPEC_POWER_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OTN_WSS_SPEC_POWER_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OTN_WSS_SPEC_POWER_REPLY);

    m_createFunc = sai_otn_wss_api->create_otn_wss_spec_power;
    m_removeFunc = sai_otn_wss_api->remove_otn_wss_spec_power;
    m_setFunc = sai_otn_wss_api->set_otn_wss_spec_power_attribute;
    m_getFunc = sai_otn_wss_api->get_otn_wss_spec_power_attribute;

}
