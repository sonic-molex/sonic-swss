#include "oscorch.h"
#include "schema.h"


extern sai_otn_osc_api_t *sai_otn_osc_api;

#define OTN_OSC_NOTIFICATION                 "OTN_OSC_NOTIFICATION"
#define OTN_OSC_REPLY                        "OTN_OSC_REPLY"
#define OTN_OSC_FLEX_COUNTER_GROUP           "OTN_OSC_FLEX_COUNTER"
#define OTN_OSC_DEFAULT_POLLING_INTERVAL_MS  1000 // ms
#define OTN_OSC_DEFAULT_ENABLED_STATE        true

OscOrch::OscOrch(DBConnector *db, const std::vector<std::string> &table_names) :
    ObjectOrch(db, table_names, (sai_object_type_t)SAI_OBJECT_TYPE_OTN_OSC, CounterType::OTN_OSC_ATTR)
{
    SWSS_LOG_ENTER();

    std::string scriptPath = "otn_osc_pluggin.lua";
    createFlexCounter(scriptPath,
                      OTN_OSC_PLUGIN_FIELD,
                      OTN_OSC_FLEX_COUNTER_GROUP,
                      StatsMode::READ,
                      OTN_OSC_DEFAULT_POLLING_INTERVAL_MS,
                      OTN_OSC_DEFAULT_ENABLED_STATE);

    m_stateTable = std::unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OTN_OSC_TABLE_NAME));
    m_nameMapTable = std::unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OTN_OSC_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OTN_OSC_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OTN_OSC_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OTN_OSC_REPLY);

    m_createFunc = sai_otn_osc_api->create_otn_osc;
    m_removeFunc = sai_otn_osc_api->remove_otn_osc;
    m_setFunc = sai_otn_osc_api->set_otn_osc_attribute;
    m_getFunc = sai_otn_osc_api->get_otn_osc_attribute;

}
