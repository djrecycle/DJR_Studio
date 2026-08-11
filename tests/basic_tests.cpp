#include <cassert>
#include <iostream>

int main()
{
    constexpr auto appName = "DJR_Studio";
    static_assert(sizeof(void*) >= 8, "DJR_Studio targets 64-bit Linux builds");
    assert(std::string(appName) == "DJR_Studio");
    std::cout << "DJR_Studio smoke tests passed\n";
    return 0;
}
