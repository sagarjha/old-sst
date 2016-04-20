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
template<class Row, Mode ImplMode, typename NameEnum, typename RowExtras>
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
template<class Row, Mode ImplMode, typename NameEnum, typename RowExtras>
class SST<Row, ImplMode, NameEnum, RowExtras>::Predicates {
        /** Type definition for a predicate: a boolean function that takes an SST as input. */
        using pred = function<bool(const SST&)>;
        /** Type definition for a trigger: a void function that takes an SST as input. */
        using trig = function<void(SST&)>;
        /** Type definition for a list of predicates, where each predicate is 
         * paired with a list of callbacks */
        using pred_list = list<pair<pred, list<trig>>>;

        using evolver = std::function<pred (const SST&, int) >;
        using evolve_trig = std::function<void (SST&, int)>;

    public:
	
		/** Predicate list for one-time predicates. */
        pred_list one_time_predicates;
		/** Predicate list for recurrent predicates */
		pred_list recurrent_predicates;
		/** Predicate list for transition predicates */
		pred_list transition_predicates;
        /** Contains one entry for every predicate in `transition_predicates`, in parallel. */
        list<bool> transition_predicate_states;

        std::vector<std::unique_ptr<std::pair<pred, int> > > evolving_preds;

        std::vector<std::unique_ptr<evolver> > evolvers;

        std::vector<std::list<evolve_trig> > evolving_triggers;

        /** Inserts a single (predicate, trigger) pair to the appropriate predicate list. */
        void insert(pred predicate, trig trigger, PredicateType type = PredicateType::ONE_TIME);

        /** Inserts a single (name, predicate, evolve) to the appropriate predicate list. */
        void insert(NameEnum name, pred predicate, evolver evolve, std::list<evolve_trig> triggers);

        void add_triggers(NameEnum name, std::list<evolve_trig> triggers);

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
template<class Row, Mode ImplMode, typename NameEnum, typename RowExtras>
void SST<Row, ImplMode, NameEnum, RowExtras>::Predicates::insert(pred predicate, trig trigger, PredicateType type) {
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

template<class Row, Mode ImplMode, typename NameEnum, typename RowExtras>
void SST<Row, ImplMode, NameEnum, RowExtras>::Predicates::insert(NameEnum name, pred predicate, evolver evolve, std::list<evolve_trig> triggers) {
	constexpr int min = std::tuple_size<SST::named_functions_t>::value;
	int index = static_cast<int>(name) - min;
	assert(index >= 0);
	assert(evolving_preds.size() == evolvers.size());
	assert(evolving_preds.size() == evolving_triggers.size());
	if (evolving_preds.size() <= index) {
		evolving_preds.resize(index+1);
		evolvers.resize(index+1);
		evolving_triggers.resize(index+1);
	}
	evolvers[index] = std::make_unique<evolver>(evolve);
	evolving_preds[index] = std::make_unique<std::pair<pred,int> >({predicate,0});
	evolving_triggers[index] = triggers;
}

template<class Row, Mode ImplMode, typename NameEnum, typename RowExtras>
void SST<Row, ImplMode, NameEnum, RowExtras>::Predicates::add_triggers(NameEnum name, std::list<evolve_trig> triggers) {
	constexpr int min = std::tuple_size<SST::named_functions_t>::value;
	int index = static_cast<int>(name) - min;
	assert(index >= 0);
	assert(index < evolving_preds.size());
	evolving_triggers[index].insert(evolving_triggers[index].end(),triggers.begin(),triggers.end());
}

} /* namespace sst */

#endif /* PREDICATES_H */
