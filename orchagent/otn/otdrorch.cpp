#include "otdrorch.h"
#include "sai_serialize.h"
#include "sai_serialize_ext.h"
#include "schema.h"


extern sai_otn_otdr_api_t *sai_otn_otdr_api;

#define OTDR_NOTIFICATION                         "OTDR_NOTIFICATION"
#define OTDR_REPLY                                "OTDR_REPLY"
#define OTDR_STAT_FLEX_COUNTER_GROUP              "OTDR_STAT_COUNTER"
#define OTDR_COUNTER_DEFAULT_POLLING_INTERVAL_MS  10000 // ms
#define OTDR_COUNTER_DEFAULT_ENABLED_STATE        true

OtdrOrch::OtdrOrch(DBConnector *db, const std::vector<std::string> &table_names) :
    ObjectOrch(db, table_names, (sai_object_type_t)SAI_OBJECT_TYPE_OTN_OTDR, CounterType::OT_OTDR_STATS)
{
    SWSS_LOG_ENTER();
#if 0
    std::string scriptPath = "otdr_stats.lua";
    createFlexCounter(scriptPath,
                      OTDR_COUNTER_STATS_LIST,
                      OTDR_STAT_FLEX_COUNTER_GROUP,
                      StatsMode::READ,
                      OTDR_COUNTER_DEFAULT_POLLING_INTERVAL_MS,
                      OTDR_COUNTER_DEFAULT_ENABLED_STATE);
#endif
    m_stateTable = std::unique_ptr<Table>(new Table(m_stateDb.get(), STATE_OTDR_TABLE_NAME));
    m_nameMapTable = std::unique_ptr<Table>(new Table(m_countersDb.get(), COUNTERS_OTDR_NAME_MAP));

    m_notificationConsumer = new NotificationConsumer(db, OTDR_NOTIFICATION);
    auto notifier = new Notifier(m_notificationConsumer, this, OTDR_NOTIFICATION);
    Orch::addExecutor(notifier);
    m_notificationProducer = new NotificationProducer(db, OTDR_REPLY);

    m_createFunc = sai_otn_otdr_api->create_otn_otdr;
    m_removeFunc = sai_otn_otdr_api->remove_otn_otdr;
    m_setFunc = sai_otn_otdr_api->set_otn_otdr_attribute;
    m_getFunc = sai_otn_otdr_api->get_otn_otdr_attribute;

}
