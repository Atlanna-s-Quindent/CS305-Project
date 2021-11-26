#include <stdint.h>
#include <vector>
#include <assert.h>
#include <memory>
#include <random>
#include <algorithm>

#define LINE_SIZE 6 //log(no of words in line) = 6
#define LINE_MASK (1<<LINE_SIZE) - 1
#define FIESTEL_DEPTH 4 // depth of fiestel network = 4
#define KEY_MASK 0x000FFFFF

class Cache
{
  public:
    Cache(uint16_t bit_sets, uint16_t bit_ways, std::string name) : 
    bit_sets(bit_sets), bit_ways(bit_ways), name(name), next_level(nullptr)
    {
        cache_tag.resize(1<<bit_sets);
        cache_valid.resize(1<<bit_sets);
        last_access.resize(1<<bit_sets);
        for(uint16_t i = 0; i < (1<<bit_sets); i++)
        {
            cache_tag[i].resize(1<<bit_ways);
            cache_valid[i].resize(1<<bit_ways);
            last_access[i].resize(1<<bit_ways);
        }
    }

    virtual int access(uint64_t address)
    {
        if (access_helper(address))
        {
            return 0;
        }
        else
        {
            int ret = 1;
            if(next_level)
                ret += next_level->access(address);
            insert(address >> LINE_SIZE);
            return ret;
        }
    }

    virtual void evict(uint64_t line_address)
    {
        uint16_t set_index = line_address & ((1<<bit_sets)-1);
        uint64_t tag = line_address >> bit_sets; // extract the tag

        // search for tag in set
        for(uint16_t i = 0; i < (1<<bit_ways); i++)
        {
            if (cache_valid[set_index][i] && cache_tag[set_index][i] == tag)
            {
                // found the tag, evict it
                cache_valid[set_index][i] = false;
                // update valid tags with last_access after evicted
                for (uint16_t j = 0; j < (1<<bit_ways); j++)
                    if (cache_valid[set_index][j] && (last_access[set_index][j] > last_access[set_index][i]))
                        last_access[set_index][j]--;

                // evict from lower levels as well
                // inclusive cache heirarchy
                for (auto it = prev_level.begin(); it != prev_level.end(); it++)
                    (*it)->evict(line_address);
                return;
            }
        }
    }

    void set_next_level(Cache* next_level_cache)
    {
        next_level = next_level_cache;
    }

    void add_prev_level(Cache* prev_level_cache)
    {
        prev_level.push_back(prev_level_cache);
    }
  
  protected:
    std::string name;
    uint16_t bit_sets, bit_ways;
    std::vector<std::vector<uint64_t> > cache_tag;
    std::vector<std::vector<bool> > cache_valid;
    std::vector<std::vector<uint16_t> > last_access;
    Cache* next_level;
    std::vector<Cache*> prev_level;

    bool access_helper(uint64_t address)
    {
        uint64_t line_address = address >> LINE_SIZE; // extract the line address
        uint16_t set_index = line_address & ((1<<bit_sets)-1);
        uint64_t tag = line_address >> bit_sets; // extract the tag
        
        // search for tag in set
        for(uint16_t i = 0; i < (1<<bit_ways); i++)
        {
            if(cache_valid[set_index][i] && cache_tag[set_index][i] == tag)
            {
                // if this is not MRU, make it and update rest
                if (last_access[set_index][i] != 1)
                {
                    last_access[set_index][i] = 0;
                    // update all
                    for (uint16_t j = 0; j < (1<<bit_ways); j++)
                        if (cache_valid[set_index][j])
                            last_access[set_index][j]++;
                }
                return true;
            }
        }
        return false;
    }

    uint16_t evict_LRU(uint16_t set_index)
    {
        uint16_t lru = 0;
        for (uint16_t i = 0; i < (1<<bit_ways); i++)
        {
            assert(cache_valid[set_index][i]); // should never call on a non empty cache
            if (last_access[set_index][i] > last_access[set_index][lru])
                lru = i;
        }
        evict( ( cache_tag[set_index][lru] << bit_sets ) | set_index );
        return lru;
    }

    void insert(uint64_t line_address)
    {
        uint16_t set_index = line_address & ((1<<bit_sets)-1);
        uint64_t tag = line_address >> bit_sets; // extract the tag
        int pos = -1;

        for(uint16_t i = 0; i < (1<<bit_ways); i++)
        {
            if(!cache_valid[set_index][i])
            {
                // found an empty slot
                pos = i;
                break;
            }
        }
        // if we reach here without pos, we have to evict
        if (pos == -1)
        {
            pos = evict_LRU(set_index);
        }

        cache_valid[set_index][pos] = true;
        cache_tag[set_index][pos] = tag;
        last_access[set_index][pos] = 0;
        // update all
        for (uint16_t j = 0; j < (1<<bit_ways); j++)
            if (cache_valid[set_index][j])
                last_access[set_index][j]++;
        

    }

};

// Fixed 15-bit set index, 16-way associative cache
// Accepts 46 bit addresses
class CeaserCache : public Cache
{
  public:
    CeaserCache(uint64_t seed, std::vector<uint32_t> key) : 
    Cache(11, 5, "CEASER-LLC"), keys(key)
    {
        assert(key.size() == FIESTEL_DEPTH);

        // only need 20 LSB
        for(uint8_t i = 0; i < FIESTEL_DEPTH; i++)
            keys[i] = KEY_MASK & keys[i];

        std::mt19937_64 rng (seed);
        
        s_box.resize(FIESTEL_DEPTH);
        inv_s_box.resize(FIESTEL_DEPTH);
        p_box.resize(FIESTEL_DEPTH);
        inv_p_box.resize(FIESTEL_DEPTH);

        // initialize permutation boxes
        for (uint16_t i = 0; i < FIESTEL_DEPTH; i++)
        {
            p_box[i].resize(40);
            inv_p_box[i].resize(40);
            for (uint8_t j = 0; j < 40; j++)
                p_box[i][j] = j;
            std::shuffle(p_box[i].begin(), p_box[i].end(), rng);
            for (uint8_t j = 0; j < 40; j++)
                inv_p_box[i][p_box[i][j]] = j;
        }

        // initialize substitution boxes
        for (uint8_t i = 0; i < FIESTEL_DEPTH; i++)
        {
            s_box[i].resize(5);
            inv_s_box[i].resize(5);
            for (uint8_t j = 0; j < 5; j++)
            {
                s_box[i][j].resize(1<<8);
                inv_s_box[i][j].resize(1<<8);
                for (uint16_t k = 0; k < (1<<8); k++)
                    s_box[i][j][k] = k;
                std::shuffle(s_box[i][j].begin(), s_box[i][j].end(), rng);
                for (uint16_t k = 0; k < (1<<8); k++)
                    inv_s_box[i][j][s_box[i][j][k]] = k;
            }
        }
    }

    int access(uint64_t address)
    {
        return Cache::access((encrypt(address >> LINE_SIZE) << LINE_SIZE) | (address & LINE_MASK));
    }

    void evict(uint64_t line_address)
    {
        uint16_t set_index = line_address & ((1<<bit_sets)-1);
        uint64_t tag = line_address >> bit_sets; // extract the tag

        // search for tag in set
        for(uint16_t i = 0; i < (1<<bit_ways); i++)
        {
            if (cache_valid[set_index][i] && cache_tag[set_index][i] == tag)
            {
                // found the tag, evict it
                cache_valid[set_index][i] = false;
                // update valid tags with last_access after evicted
                for (uint16_t j = 0; j < (1<<bit_ways); j++)
                    if (cache_valid[set_index][j] && (last_access[set_index][j] > last_access[set_index][i]))
                        last_access[set_index][j]--;

                // evict from lower levels as well
                // inclusive cache heirarchy
                for (auto it = prev_level.begin(); it != prev_level.end(); it++)
                    (*it)->evict(decrypt(line_address));
                return;
            }
        }
    }

  protected:
    std::vector<uint32_t> keys;
    std::vector<std::vector<std::vector<uint8_t> > > s_box;
    std::vector<std::vector<std::vector<uint8_t> > > inv_s_box;
    std::vector<std::vector<uint8_t> > p_box;
    std::vector<std::vector<uint8_t> > inv_p_box;

    uint64_t encrypt(uint64_t line_address)
    {
        uint64_t input = line_address, output = 0;
        for (uint8_t i = 0; i < FIESTEL_DEPTH; i++)
        {
            output = 0;
            for (uint8_t j = 0; j < 5; j++)
                output |= (uint64_t)s_box[i][j][(input >> (8*j)) & 0xFF] << (8*j);
            input = output;
            output = 0;
            for (uint8_t j = 0; j < 40; ++j)
                output |= ((input >> j) & 1) << p_box[i][j]; 
            input = output;
        }
        return output;
    }

    uint64_t decrypt(uint64_t cipher_address)
    {
        uint64_t input = cipher_address, output = 0;
        for (int i = FIESTEL_DEPTH-1; i >= 0; i--)
        {
            output = 0;
            for (uint8_t j = 0; j < 40; ++j)
                output |= ((input >> j) & 1) << inv_p_box[i][j]; 
            input = output;
            output = 0;
            for (uint8_t j = 0; j < 5; j++)
                output |= (uint64_t)inv_s_box[i][j][(input >> (8*j)) & 0xFF] << (8*j);
            input = output;
        }
        return output;
    }
};