#include "attenuatororch.h"
#include "schema.h"


extern sai_otn_attenuator_api_t *sai_otn_attenuator_api;

#define OTN_ATTENUATOR_NOTIFICATION                         "OTN_ATTENUATOR_NOTIFICATION"
#define OTN_ATTENUATOR_REPLY                                "OTN_ATTENUATOR_REPLY"
#define OTN_ATTENUATOR_FLEX_COUNTER_GROUP                   "OTN_ATTENUATOR_FLEX_COUNTER"
#define OTN_ATTENUATOR_PLUGIN_DEFAULT_POLLING_INTERVAL_MS   1000 // ms
#define OTN_ATTENUATOR_PLUGIN_DEFAULT_ENABLED_STATE         true

AttenuatorOrch::AttenuatorOrch(DBConnector *db, const std::vector<std::string> &table_names) :
    ObjectOrch(db, table_names, (sai_object_type_t)SAI_OBJECT_TYPE_OTN_ATTENUATOR, CounterType::OTN_ATTENUATOR_ATTR)
{
    SWSS_LOG_ENTER();

    std::string scriptPath = "otn_attenuator_pluggin.lua";
    createFlexCounter(scriptPath,
                      OTN_ATTENUATOR_PLUGIN_FIELD,
                      OTN_ATTENUATOR_FLEX_COUNTER_GROUP,
                      StatsMode::READ,
                      OTN_ATTENUATOR_PLUGIN_DEFAULT_POLLING_INTERVAL_MS,
                      OTN_ATTENUATOR_PLUGIN_DEFAULT_ENABLED_STATE);

    m_stateTable = std::unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OTN_ATTENUATOR_TABLE_NAME));
    m_nameMapTable = std::unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OTN_ATTENUATOR_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OTN_ATTENUATOR_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OTN_ATTENUATOR_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OTN_ATTENUATOR_REPLY);

    m_createFunc = sai_otn_attenuator_api->create_otn_attenuator;
    m_removeFunc = sai_otn_attenuator_api->remove_otn_attenuator;
    m_setFunc = sai_otn_attenuator_api->set_otn_attenuator_attribute;
    m_getFunc = sai_otn_attenuator_api->get_otn_attenuator_attribute;

}
