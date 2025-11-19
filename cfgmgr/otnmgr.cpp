#include "logger.h"
#include "dbconnector.h"
#include "tokenize.h"
#include "ipprefix.h"
#include "otnmgr.h"
#include "exec.h"
#include "shellcmd.h"
#include <swss/redisutility.h>
#include <unistd.h>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace std;
using namespace swss;

OtnMgr::OtnMgr(DBConnector *cfgDb, DBConnector *appDb, DBConnector *stateDb, const std::vector<std::string> &tableNames, const std::map<std::string, std::string> &tableMaps) :
        Orch(cfgDb, tableNames),
        m_appl_db(appDb),
        m_state_db(stateDb),
        m_tableMaps(tableMaps)
{
    loadDeviceExtraMap();
}

void OtnMgr::doTask(Consumer &consumer)
{
    SWSS_LOG_ENTER();

    string cfgName = consumer.getTableName();

    /* get app table by name */
    auto itApp = m_tableMaps.find(cfgName);
    if (itApp == m_tableMaps.end())
    {
        SWSS_LOG_ERROR("OtnMgr|%s is invalid", cfgName.c_str());
        return;
    }
    const string &appName = itApp->second;
    shared_ptr<ProducerStateTable> appTable;
    auto itTable = m_appTables.find(appName);
    if (itTable == m_appTables.end())
    {
        appTable = make_shared<ProducerStateTable>(m_appl_db, appName);
        m_appTables[appName] = appTable;
    }
    else
    {
        appTable = itTable->second;
    }

    auto it = consumer.m_toSync.begin();
    while (it != consumer.m_toSync.end())
    {
        KeyOpFieldsValuesTuple t = it->second;
        string alias = kfvKey(t);
        string op = kfvOp(t);

        // query device status, skip if device is offline
        if (!isOnline(alias))
        {
            it = consumer.m_toSync.erase(it);
            continue;
        }

        SWSS_LOG_NOTICE("OtnMgr doTask, cfg=%s, app=%s, key=%s, op=%s", cfgName.c_str(), appName.c_str(), alias.c_str(), op.c_str());

        if (op == SET_COMMAND)
        {
            auto values = kfvFieldsValues(t);
            for (auto value : values)
            {
                SWSS_LOG_NOTICE("OtnMgr doTask, key=%s, value=%s", value.first.c_str(), value.second.c_str());
            }
            if (values.size())
            {
                writeConfigToAppDb(appTable, alias, values);
            }
        }
        else if (op == DEL_COMMAND)
        {
            SWSS_LOG_NOTICE("Delete component: %s", alias.c_str());
            appTable->del(alias);
        }

        it = consumer.m_toSync.erase(it);
    }
}

bool OtnMgr::writeConfigToAppDb(std::shared_ptr<ProducerStateTable> &table, const std::string &alias, const std::string &field, const std::string &value)
{
    SWSS_LOG_ENTER();

    vector<FieldValueTuple> fvs;
    FieldValueTuple fv(field, value);
    fvs.push_back(fv);
    table->set(alias, fvs);

    return true;
}

bool OtnMgr::writeConfigToAppDb(std::shared_ptr<ProducerStateTable> &table, const std::string &alias, std::vector<FieldValueTuple> &field_values)
{
    SWSS_LOG_ENTER();

    table->set(alias, field_values);
    return true;
}

bool OtnMgr::isOnline(const std::string &alias)
{
    SWSS_LOG_ENTER();

    SWSS_LOG_DEBUG("Query device status for alias %s", alias.c_str());

    auto dev_name = queryDeviceName(alias);
    if (!queryDeviceStatus(dev_name))
    {
        SWSS_LOG_NOTICE("Device %s is offline, alias %s", dev_name.c_str(), alias.c_str());
        return false;
    }

    SWSS_LOG_NOTICE("Device %s is online, alias %s", dev_name.c_str(), alias.c_str());

    return true;
}

bool OtnMgr::queryDeviceStatus(const std::string &dev_name)
{
    SWSS_LOG_ENTER();

    SWSS_LOG_NOTICE("Query device status for %s", dev_name.c_str());

    static const std::string linecard_table("LINECARD_INFO_TABLE");
    static const std::string sep = SonicDBConfig::getSeparator(m_state_db);
    static const std::string status_online = "Online";

    std::string key = linecard_table + sep + dev_name;

    if (m_state_db->exists(key))
    {
        /* query status */
        auto hash = m_state_db->hgetall(key);

        auto status = hash.find("status");
        if (status != hash.end() && status_online == status->second)
        {
            SWSS_LOG_INFO("Device %s is %s", dev_name.c_str(), status_online.c_str());
            return true;
        }
    }

    SWSS_LOG_NOTICE("%s is offline, key %s, skip", dev_name.c_str(), key.c_str());

    return false;
}

std::string OtnMgr::queryDeviceName(const std::string &alias)
{
    SWSS_LOG_ENTER();

    for (const auto& it : m_deviceExtraMap) {
        for (const auto& v : it.second) {
            if (v == alias) {
                // Found
                return it.first;
            }
        }
    }

    // Not found, return alias itself
    return alias;
}

bool OtnMgr::loadDeviceExtraMap()
{
    SWSS_LOG_ENTER();

    std::string path("/usr/share/sonic/platform/linecard_extra_map.json");
    std::ifstream ifs(path);

    if (ifs.fail())
    {
        SWSS_LOG_ERROR("OTN init file %s not found", path.c_str());
        return false;
    }

    nlohmann::json j = nlohmann::json::parse(ifs);
    // Convert to map<string, vector<string>>
    for (auto& item : j.items()) {
        const std::string& key = item.key();
        const auto& value = item.value();
        m_deviceExtraMap[key] = value.get<std::vector<std::string>>();
    }

    ifs.close();

    SWSS_LOG_NOTICE("Load ONT device extra mapping, size=%zu", m_deviceExtraMap.size());

    return true;
}
