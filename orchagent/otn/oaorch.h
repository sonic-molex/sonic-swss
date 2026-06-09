#pragma once

#include "objectorch.h"

class OaOrch: public ObjectOrch
{
public:
    OaOrch(DBConnector *db, const std::vector<std::string> &table_names);
};
