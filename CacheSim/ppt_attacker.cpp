#include "controller.hpp"
#include <iostream>

#include <random>
#include <vector>

using namespace std;

#define LLC_SETS 2048
#define LLC_WAYS 32

int main()
{
    Controller c;

    // generate uniform lines
    uniform_int_distribution<uint64_t> dist(1, ((uint64_t)1<<40)-1);
    mt19937_64 gen(0);
    vector<uint64_t> addresses(2 * LLC_SETS * LLC_WAYS);
    for (auto& addr : addresses)
        addr = dist(gen) << LINE_SIZE;
    
    // linearly probe all lines
    for (auto& addr : addresses)
        c.access(addr);

    vector<uint64_t> pruned;
    // probe in reverse order to prune
    for (auto it = addresses.rbegin(); it != addresses.rend(); ++it)
        if ( c.access(*it) < 3 )
            pruned.push_back(*it);
    
    reverse(pruned.begin(), pruned.end());
    // refill with pruned set
    for (auto& addr : pruned)
        c.access(addr);

    uint64_t target_line = 0;
    c.access(target_line << LINE_SIZE);

    vector<uint64_t> eviction_set;
    // access pruned set again
    // whatever misses forms an eviciton set
    for (auto& addr : pruned)
        if ( c.access(addr) == 3 )
            eviction_set.push_back(addr);

    // log eviction set
    std::cout << "For target line address " << target_line << LINE_SIZE << std::endl;
    std::cout << "Eviction set size " << eviction_set.size() << std::endl;
    std::cout << "Eviction set :" << std::endl;
    for (auto& addr : eviction_set)
        std::cout << addr << std::endl;

    // verification
    std::cout << "Inserting " << target_line << std::endl;
    c.access(target_line << LINE_SIZE);
    std::cout << "Inserting eviction set" << std::endl;
    for (auto& addr : eviction_set)
        c.access(addr);
    std::cout << "Accessing target" << std::endl;
    if (c.access(target_line << LINE_SIZE) == 3)
        std::cout << "Target line evicted" << std::endl;
    else
    {
        std::cout << "Target line not evicted" << std::endl;
        std::cout << "Experiment failed" << std::endl;
        return 1;
    }
    
    std::cout << "Accessing eviction set in series" << std::endl;
    for (auto& addr : eviction_set)
        if (c.access(addr) != 3)
        {
            std::cout << "Eviction set not evicted" << std::endl;
            std::cout << "Experiment failed" << std::endl;
            return 1;
        }
    std::cout << "Experiment successful" << std::endl;

}