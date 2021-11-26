#include "cache.hpp"

class Controller {
    Cache L1D, L2D;
    CeaserCache LLC;
  public:
    Controller() : L1D(8,3, "L1D"), L2D(10,4, "L2D"), LLC(0, {100,200,300,400})
    {
        L1D.set_next_level(&L2D);
        L2D.add_prev_level(&L1D);
        L2D.set_next_level(&LLC);
        LLC.add_prev_level(&L2D);
    }
    int access(uint64_t addr)
    {
        assert((addr >> 46) == 0);
        return L1D.access(addr);
    }
};