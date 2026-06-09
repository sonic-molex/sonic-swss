#pragma once

#include "objectorch.h"

class OscOrch: public ObjectOrch
{
public:
    OscOrch(DBConnector *db, const std::vector<std::string> &table_names);
};
