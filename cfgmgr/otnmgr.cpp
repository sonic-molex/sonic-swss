#include "otnmgr.h"

using namespace std;
using namespace swss;

OtnMgr::OtnMgr(DBConnector *cfgDb, DBConnector *appDb, const std::vector<std::string> &tableNames, const std::map<std::string, std::string> &tableMaps) :
        Orch(cfgDb, tableNames),
        m_appl_db(appDb),
        m_tableMaps(tableMaps)
{
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
        auto &t = it->second;
        string alias = kfvKey(t);
        string op = kfvOp(t);

        SWSS_LOG_NOTICE("OtnMgr doTask, cfg=%s, app=%s, key=%s, op=%s", cfgName.c_str(), appName.c_str(), alias.c_str(), op.c_str());

        if (op == SET_COMMAND)
        {
            auto values = kfvFieldsValues(t);
            for (const auto &value : values)
            {
                SWSS_LOG_NOTICE("OtnMgr doTask, key=%s, value=%s", value.first.c_str(), value.second.c_str());
            }
            if (!values.empty())
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

void OtnMgr::writeConfigToAppDb(std::shared_ptr<ProducerStateTable> &table, const std::string &alias, const std::vector<FieldValueTuple> &field_values)
{
    SWSS_LOG_ENTER();

    table->set(alias, field_values);
}
