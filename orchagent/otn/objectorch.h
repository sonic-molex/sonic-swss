#pragma once

#include <string>
#include <vector>
#include <map>
#include <tuple>
#include "orch.h"
#include "saihelper.h"
#include "notifier.h"
#include "notificationproducer.h"
#include "notifications.h"
#include "timer.h"
#include "flex_counter_manager.h"

using namespace swss;

typedef sai_status_t (*CreateObjectFunc)(
        sai_object_id_t *oid,
        sai_object_id_t linecard_id,
        uint32_t attr_count,
        const sai_attribute_t *attr_list);

typedef sai_status_t (*RemoveObjectFunc)(
        sai_object_id_t oid);

typedef sai_status_t (*SetObjectAttrFunc)(
        sai_object_id_t oid,
        const sai_attribute_t *attr);

typedef sai_status_t (*GetObjectAttrFunc)(
        sai_object_id_t oid,
        uint32_t attr_count,
        sai_attribute_t *attr_list);

typedef enum _ConfigState_E
{
    CONFIG_MISSING = 0,
    CONFIG_RECEIVED,
    CONFIG_CREATED,
    CONFIG_DONE,
} ConfigState_E;

class ObjectOrch: public Orch
{
public:
    ObjectOrch(DBConnector *db, const std::vector<std::string> &table_names): Orch(db, table_names) {}

    ObjectOrch(DBConnector *db,
               const std::vector<std::string> &table_names,
               sai_object_type_t obj_type,
               CounterType flex_counter_type);

    ObjectOrch(DBConnector *db,
               std::vector<TableConnector> &connectors,
               sai_object_type_t obj_type,
               CounterType flex_counter_type);

    void localDataInit(DBConnector *db);

    void doTask(Consumer &consumer);

    virtual void doTask(NotificationConsumer &consumer);

    void doStateTask(Consumer &consumer);

    bool createObject(const std::string &key);
    bool removeObject(const std::string &key);

    virtual void addExtraAttrsOnCreate(const std::string &key, std::vector<sai_attribute_t> &attrs) {};

    bool setObjectAttrs(const std::string &key,
                            std::map<std::string, std::string> &field_values,
                            std::string operation_id="");

    sai_status_t setObjectAttr(sai_object_id_t oid, const std::string &field, const std::string &value);

    sai_status_t getObjectAttr(sai_object_id_t oid, const std::string &field, std::string &value);

    virtual void setFlexCounter(sai_object_id_t id);

    virtual void clearFlexCounter(sai_object_id_t id);

    virtual void doSubobjectStateTask(const std::string &key, const std::string &present){};

    void publishOperationResult(std::string operation_id, int status_code, std::string message);

    bool translateObjectAttr(_In_ const std::string &field,
                                 _In_ const std::string &value,
                                 _Out_ sai_attribute_t &attr);

    bool createFlexCounter(_In_ const std::string &script_path,
                           _In_ const std::string &plugin_field,
                           _In_ const std::string &group_name,
                           _In_ const StatsMode stats_mode,
                           _In_ const uint polling_interval,
                           _In_ const bool enabled);

    void copyConfigToState(const std::string &key, const FieldValueTuple &fv);
    void copyConfigToState(const std::string &key, std::map<std::string, std::string> &fvs);

protected:

    std::shared_ptr<DBConnector> m_stateDb;

    std::unique_ptr<Table> m_stateTable;

    std::shared_ptr<DBConnector> m_countersDb;

    std::unique_ptr<Table> m_nameMapTable;

    std::unique_ptr<Table> m_vid2NameTable;

    CreateObjectFunc m_createFunc;

    RemoveObjectFunc m_removeFunc;

    SetObjectAttrFunc m_setFunc;

    GetObjectAttrFunc m_getFunc;

    uint32_t m_count;

    sai_object_type_t m_objectType;

    std::string m_objectName;

    CounterType m_flex_counter_type;

    /* Attributes that can be modified at anytime. */
    std::map<std::string, sai_attr_id_t> m_createandsetAttrs;

    /* Attributes that can only be set during creation. */
    std::map<std::string, sai_attr_id_t> m_createonlyAttrs;

    std::map<std::string, sai_attr_id_t> m_mandatoryAttrs;

    std::map<std::string, sai_attr_id_t> m_readonlyAttrs;

    std::map<std::string, std::string> m_enumValues;

    /* record original name instead of short name above */
    std::map<std::string, sai_attr_id_t> m_readonlyOrgAttrs;

    /* record precision */
    std::map<std::string, size_t> m_attrPrecisions;

    ConfigState_E m_configState = CONFIG_MISSING;

    std::set<std::string> m_keys;

    std::map<std::string, sai_object_id_t> m_key2oid;

    std::map<std::string, std::map<std::string, std::string>> m_key2createonlyAttrs;

    std::map<std::string, std::map<std::string, std::string>> m_key2createandsetAttrs;

    std::map<std::string, std::string> m_key2present;

    std::set<std::string> m_needToCache;

    NotificationConsumer *m_notificationConsumer;

    NotificationProducer *m_notificationProducer;

    FlexCounterManager *m_flex_stat_manager;
};
