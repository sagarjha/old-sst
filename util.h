/*
 * util.h
 *
 *  Created on: Mar 16, 2016
 *      Author: edward
 */

#ifndef UTIL_H_
#define UTIL_H_

namespace sst {

/**
 * Assorted utility classes and functions that support SST internals.
 */
namespace util {

/**
 * Base case for the recursive template_and function.
 * @return True
 */
constexpr bool template_and(){
    return true;
}

/**
 * Computes the logical AND of a list of parameters, at compile-time so it can
 * be used in template parameters.
 * @param first The first Boolean value
 * @param rest The rest of the Boolean values
 * @return True if every argument evaluates to True, False otherwise.
 */
template<typename... rest_t>
constexpr bool template_and(bool first, rest_t... rest){
    return first && template_and(rest...);
}

/** An empty enum class, used to provide a default "none" argument for template parameters. */
enum class NullEnum {};

}
}




#endif /* UTIL_H_ */
