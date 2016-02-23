#ifndef PREDICATES_H
#define PREDICATES_H

#include <functional>
#include <list>
#include <utility>

namespace sst {

using std::function;
using std::list;
using std::pair;

template<class Row>
class SST;
template<class Row>
class SST_reads;
template<class Row>
class SST_writes;

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

/**
 * Predicates class for SST-reads.
 */
template<class Row>
class Predicates_read {
		/** Predicate type for SST-reads */
        typedef function<bool(SST_reads<Row>*)> pred_read;
		/** Trigger type for SST-reads */
        typedef function<void(SST_reads<Row>*)> trig_read;
        /** Type definition for a list of predicates, where each predicate is 
		 * paired with a list of callbacks */
        typedef list<pair<pred_read, list<trig_read>>> pred_list_read;

    public:
		/** Predicate list for one-time predicates. */
        pred_list_read one_time_predicates; 
		/** Predicate list for recurrent predicates */
		pred_list_read recurrent_predicates; 
		/** Predicate list for transition predicates */
		pred_list_read transition_predicates; 
        /** Contains one entry for every predicate in `transition_predicates`, in parallel. */
        list<bool> transition_predicate_states;

        /** Inserts a single (predicate, trigger) pair to the appropriate predicate list. */
        void insert(pred_read predicate, trig_read trigger, PredicateType type =
                PredicateType::ONE_TIME);
};

/**
 * Predicates class for SST-writes.
 */
template<class Row>
class Predicates_write {
		/** Predicate type for SST-writes */
        typedef function<bool(SST_writes<Row>*)> pred_write;
		/** Trigger type for SST-writes */
        typedef function<void(SST_writes<Row>*)> trig_write;
        /** Type definition for a list of predicates, where each predicate is 
		 * paired with a list of callbacks */
        typedef list<pair<pred_write, list<trig_write>>> pred_list_write;

    public:
        /** @copydoc Predicates_read::one_time_predicates */
		pred_list_write one_time_predicates;
        /** @copydoc Predicates_read::recurrent_predicates */
		pred_list_write recurrent_predicates;
        /** @copydoc Predicates_read::transition_predicates */
		pred_list_write transition_predicates;
		/** @copydoc Predicates_read::transition_predicate_states */
        list<bool> transition_predicate_states;

        /** Inserts a single (predicate, trigger) pair to the appropriate predicate list. */
        void insert(pred_write predicate, trig_write trigger, PredicateType type = PredicateType::ONE_TIME);
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
template<class Row>
void Predicates_read<Row>::insert(pred_read predicate, trig_read trigger,
        PredicateType type) {
    list<trig_read> g_list;
    g_list.push_back(trigger);
    if (type == PredicateType::ONE_TIME) {
        one_time_predicates.push_back(
                pair<pred_read, list<trig_read> >(predicate, g_list));
    } else if (type == PredicateType::RECURRENT) {
        recurrent_predicates.push_back(
                pair<pred_read, list<trig_read> >(predicate, g_list));
    } else {
        transition_predicates.push_back(
                pair<pred_read, list<trig_read> >(predicate, g_list));
        transition_predicate_states.push_back(false);
    }
}

/**
 * @copydetails Predicates_read::insert()
 */
template<class Row>
void Predicates_write<Row>::insert(pred_write predicate, trig_write trigger,
        PredicateType type) {
    list<trig_write> g_list;
    g_list.push_back(trigger);
    if (type == PredicateType::ONE_TIME) {
        one_time_predicates.push_back(
                pair<pred_write, list<trig_write> >(predicate, g_list));
    } else if (type == PredicateType::RECURRENT) {
        recurrent_predicates.push_back(
                pair<pred_write, list<trig_write> >(predicate, g_list));
    } else {
        transition_predicates.push_back(
                pair<pred_write, list<trig_write> >(predicate, g_list));
        transition_predicate_states.push_back(false);
    }
}
}

#endif
