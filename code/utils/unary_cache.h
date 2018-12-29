//
// Created by ckielwein on 29.12.18.
//

#ifndef FS2_OPEN_UNARY_CACHE_H
#define FS2_OPEN_UNARY_CACHE_H

#include <functional>

/**
 * @brief stores the value last aquired with its key.
 * @tparam Key Type of Key used to query values.
 * @tparam Value Type of Value stored in cache.
 * @param Getter type of function or function object used to get Values from Keys.
 * Default is a function pointer of signature Value(Key)
 *
 * Unary Cache is the simplest caching scheme possible.
 * It only ever stores one instance of Value and Key.
 * It is meant as a performance optimization when searching for values is expensive.
 * It will only help if the same key-value pair are used after another.
 * It should thus only ever be used together with profiling to see if its use pays out.
 */
template<class Key, class Value, class Getter = Value (*)(Key)>
class UnaryCache {
public:

    explicit UnaryCache(Getter getter = Getter(), Key init_key = Key(), Value init_value = Value())
            : get_function(std::move(getter))
            , lastKey(std::move(init_key))
            ,lastValue(std::move(init_value))
            {
            }
    ~UnaryCache() = default;

    /// returns Value corresponding to key, might or might not call Getter.
    Value get(const Key& key) {
        if (key != lastKey) {
            lastValue = get_function(key);
            lastKey = key;
        }
        return lastValue;
    }

private:
    Key lastKey;
    Value lastValue;
    Getter get_function;
};

#endif //FS2_OPEN_UNARY_CACHE_H