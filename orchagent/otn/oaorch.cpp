#include "oaorch.h"
#include "schema.h"


extern sai_otn_oa_api_t *sai_otn_oa_api;

#define OTN_OA_NOTIFICATION                         "OTN_OA_NOTIFICATION"
#define OTN_OA_REPLY                                "OTN_OA_REPLY"
#define OTN_OA_FLEX_COUNTER_GROUP                   "OTN_OA_FLEX_COUNTER"
#define OTN_OA_DEFAULT_POLLING_INTERVAL_MS          1000 // ms
#define OTN_OA_DEFAULT_ENABLED_STATE                true

OaOrch::OaOrch(DBConnector *db, const std::vector<std::string> &table_names) :
    ObjectOrch(db, table_names, (sai_object_type_t)SAI_OBJECT_TYPE_OTN_OA, CounterType::OTN_OA_ATTR)
{
    SWSS_LOG_ENTER();

    std::string scriptPath = "otn_oa_pluggin.lua";
    createFlexCounter(scriptPath,
                      OTN_OA_PLUGIN_FIELD,
                      OTN_OA_FLEX_COUNTER_GROUP,
                      StatsMode::READ,
                      OTN_OA_DEFAULT_POLLING_INTERVAL_MS,
                      OTN_OA_DEFAULT_ENABLED_STATE);

    m_stateTable = std::unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OTN_OA_TABLE_NAME));
    m_nameMapTable = std::unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OTN_OA_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OTN_OA_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OTN_OA_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OTN_OA_REPLY);

    m_createFunc = sai_otn_oa_api->create_otn_oa;
    m_removeFunc = sai_otn_oa_api->remove_otn_oa;
    m_setFunc = sai_otn_oa_api->set_otn_oa_attribute;
    m_getFunc = sai_otn_oa_api->get_otn_oa_attribute;

}
