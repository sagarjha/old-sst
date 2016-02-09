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

enum class PredicateType {
    ONE_TIME, RECURRENT, TRANSITION
};

// predicates class for reads
template<class Row>
class Predicates_read {
        typedef function<bool(SST_reads<Row>*)> pred_read;
        typedef function<void(SST_reads<Row>*)> trig_read;
        // list of a predicate paired with a list of callbacks
        typedef list<pair<pred_read, list<trig_read>>> pred_list_read;

    public:
        pred_list_read one_time_predicates, recurrent_predicates, transition_predicates;
        //Contains one entry for every predicate in transition_predicates, in parallel
        list<bool> transition_predicate_states;

        // inserts a (pred,trig) pair to the list of predicates
        void insert(pred_read f, trig_read g, PredicateType type =
                PredicateType::ONE_TIME);
};

// predicates class for writes
template<class Row>
class Predicates_write {
        typedef function<bool(SST_writes<Row>*)> pred_write;
        typedef function<void(SST_writes<Row>*)> trig_write;
        typedef list<pair<pred_write, list<trig_write>>> pred_list_write;

    public:
        // list of predicates and triggers
        pred_list_write one_time_predicates, recurrent_predicates, transition_predicates;
        //Contains one entry for every predicate in transition_predicates, in parallel
        list<bool> transition_predicate_states;

        // inserts a (pred,trig) pair to the list of predicates
        void insert(pred_write f, trig_write g, PredicateType type = PredicateType::ONE_TIME);
};

// insert into different lists based on the type
template<class Row>
void Predicates_read<Row>::insert(pred_read f, trig_read g,
        PredicateType type) {
    list<trig_read> g_list;
    g_list.push_back(g);
    if (type == PredicateType::ONE_TIME) {
        one_time_predicates.push_back(
                pair<pred_read, list<trig_read> >(f, g_list));
    } else if (type == PredicateType::RECURRENT) {
        recurrent_predicates.push_back(
                pair<pred_read, list<trig_read> >(f, g_list));
    } else {
        transition_predicates.push_back(
                pair<pred_read, list<trig_read> >(f, g_list));
        transition_predicate_states.push_back(false);
    }
}

// insert into different lists based on the type
template<class Row>
void Predicates_write<Row>::insert(pred_write f, trig_write g,
        PredicateType type) {
    list<trig_write> g_list;
    g_list.push_back(g);
    if (type == PredicateType::ONE_TIME) {
        one_time_predicates.push_back(
                pair<pred_write, list<trig_write> >(f, g_list));
    } else if (type == PredicateType::RECURRENT) {
        recurrent_predicates.push_back(
                pair<pred_write, list<trig_write> >(f, g_list));
    } else {
        transition_predicates.push_back(
                pair<pred_write, list<trig_write> >(f, g_list));
        transition_predicate_states.push_back(false);
    }
}
}

#endif
