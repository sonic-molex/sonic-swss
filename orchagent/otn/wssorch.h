#pragma once

#include "objectorch.h"

class WssOrch: public ObjectOrch
{
public:
    WssOrch(DBConnector *db, const std::vector<std::string> &table_names);
};


class WssSpecPowerOrch: public ObjectOrch
{
public:
    WssSpecPowerOrch(DBConnector *db, const std::vector<std::string> &table_names);
};
