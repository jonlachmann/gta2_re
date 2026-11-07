// Port (docs/port.md): the registry of port_globals.hpp.
#include "port_globals.hpp"
#include <vector>

namespace
{
std::vector<const PortGlobal*>& List()
{
    static std::vector<const PortGlobal*> sList; // built during static initialisation
    return sList;
}
} // namespace

PortGlobal::PortGlobal(const void* pVar, unsigned int ogAddr, unsigned int size, const char* pName, unsigned int pointeeSize) :
    mVar(pVar), mOgAddr(ogAddr), mSize(size), mName(pName), mPointeeSize(pointeeSize)
{
    List().push_back(this);
}

const PortGlobal* const* PortGlobals(size_t* pCount)
{
    *pCount = List().size();
    return List().empty() ? 0 : &List()[0];
}
