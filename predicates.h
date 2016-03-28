#ifndef PREDICATES_H
#define PREDICATES_H

#include <functional>
#include <list>
#include <utility>

#include "sst.h"

namespace sst {

using std::function;
using std::list;
using std::pair;

/** Enumeration defining the kinds of predicates an SST can handle. */
enum class PredicateType {
	/** One-time predicates only fire once; they are deleted once they become true. */
    ONE_TIME, 
	/** Recurrent predicates persist as long as the SST instance and fire their
	 * triggers every time they are true. */
	RECURRENT, 
	/** Transition predicates persist as long as the SST instance, but only fire
	 * their triggers when they transition from false to true. */
	TRANSITION
};

enum class Mode;
template<class Row, Mode ImplMode, typename NameEnum, typename NamedFunctionTypePack, typename NamedRowPredicatePack>
class SST;

/**
 * Predicates container for SST. The template parameters must match the SST for
 * which the predicates are being used.
 *
 * @tparam Row The type of the structure that will be used for each row in
 * the SST
 * @tparam Mode A {@link Mode} enum value indicating whether the SST is in
 * Reads mode or Writes mode
 */
template<class Row, Mode ImplMode, typename NameEnum, typename NamedFunctionTypePack, typename NamedRowPredicatePack>
class SST<Row, ImplMode, NameEnum, NamedFunctionTypePack, NamedRowPredicatePack>::Predicates {
		/** Type definition for a predicate: a boolean function that takes an SST as input. */
        using pred = function<bool(const SST&)>;
		/** Type definition for a trigger: a void function that takes an SST as input. */
        using trig = function<void(SST&)>;
        /** Type definition for a list of predicates, where each predicate is 
		 * paired with a list of callbacks */
        using pred_list = list<pair<pred, list<trig>>>;

    public:
		/** Predicate list for one-time predicates. */
        pred_list one_time_predicates;
		/** Predicate list for recurrent predicates */
		pred_list recurrent_predicates;
		/** Predicate list for transition predicates */
		pred_list transition_predicates;
        /** Contains one entry for every predicate in `transition_predicates`, in parallel. */
        list<bool> transition_predicate_states;

        /** Inserts a single (predicate, trigger) pair to the appropriate predicate list. */
        void insert(pred predicate, trig trigger, PredicateType type =
                PredicateType::ONE_TIME);
};


/**
 * This is a convenience method for when the predicate has only one trigger; it
 * automatically chooses the right list based on the predicate type. To insert
 * a predicate with multiple triggers, use std::list::insert() directly on the 
 * appropriate predicate list member.
 * @param predicate The predicate to insert.
 * @param trigger The trigger to execute when the predicate is true.
 * @param type The type of predicate being inserted; default is 
 * PredicateType::ONE_TIME
 */
template<class Row, Mode ImplMode, typename NameEnum, typename NamedFunctionTypePack, typename NamedRowPredicatePack>
void SST<Row, ImplMode, NameEnum, NamedFunctionTypePack, NamedRowPredicatePack>::Predicates::insert(pred predicate, trig trigger, PredicateType type) {
    list<trig> g_list;
    g_list.push_back(trigger);
    if (type == PredicateType::ONE_TIME) {
        one_time_predicates.push_back(
                pair<pred, list<trig>>(predicate, g_list));
    } else if (type == PredicateType::RECURRENT) {
        recurrent_predicates.push_back(
                pair<pred, list<trig>>(predicate, g_list));
    } else {
        transition_predicates.push_back(
                pair<pred, list<trig>>(predicate, g_list));
        transition_predicate_states.push_back(false);
    }
}

} /* namespace sst */

#endif /* PREDICATES_H */
