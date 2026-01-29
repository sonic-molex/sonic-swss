#include "wssorch.h"
#include "schema.h"


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

    m_notificationConsumer = new NotificationConsumer(db, OTN_WSS_SPEC_POWER_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OTN_WSS_SPEC_POWER_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OTN_WSS_SPEC_POWER_REPLY);

    m_createFunc = sai_otn_wss_api->create_otn_wss_spec_power;
    m_removeFunc = sai_otn_wss_api->remove_otn_wss_spec_power;
    m_setFunc = sai_otn_wss_api->set_otn_wss_spec_power_attribute;
    m_getFunc = sai_otn_wss_api->get_otn_wss_spec_power_attribute;

}
