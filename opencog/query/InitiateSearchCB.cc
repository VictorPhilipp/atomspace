/*
 * InitiateSearchCB.cc
 *
 * Copyright (C) 2015 Linas Vepstas
 *
 * Author: Linas Vepstas <linasvepstas@gmail.com>  April 2015
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License v3 as
 * published by the Free Software Foundation and including the exceptions
 * at http://opencog.org/wiki/Licenses
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program; if not, write to:
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <opencog/atoms/execution/EvaluationLink.h>
#include <opencog/atomutils/FindUtils.h>
#include <opencog/atoms/bind/BetaRedex.h>

#include "InitiateSearchCB.h"
#include "PatternMatchEngine.h"

using namespace opencog;

// Uncomment below to enable debug print
// #define DEBUG
#ifdef DEBUG
#define dbgprt(f, varargs...) printf(f, ##varargs)
#else
#define dbgprt(f, varargs...)
#endif

/* ======================================================== */

InitiateSearchCB::InitiateSearchCB(AtomSpace* as) :
	_classserver(classserver()),
	_variables(NULL),
	_pattern(NULL),
	_type_restrictions(NULL),
	_dynamic(NULL),
	_as(as)
{
}

/* ======================================================== */

// Find a good place to start the search.
//
// The handle h points to a clause.  In principle, it is enough to
// simply find a constant in the clause, and just start there. In
// practice, this can be an awful way to do things. So, for example,
// most "typical" clauses will be of the form
//
//    EvaluationLink
//        PredicateNode "blah"
//        ListLink
//            VariableNode $var
//            ConceptNode  "item"
//
// Typically, the incoming set for "blah" will be huge, so starting the
// search there would be a poor choice. Typically, the incoming set to
// "item" will be much smaller, and so makes a better choice.  The code
// below tries to pass over "blah" and pick "item" instead.  It does so
// by comparing the size of the incoming sets of the two constants, and
// picking the one with the smaller ("thinner") incoming set. Note that
// this is a form of "greedy" search.
//
// Atoms that are inside of dynamically-evaluatable terms are not
// considered. That's because groundings for such terms might not exist
// in the atomspace, so a search that starts there is doomed to fail.
//
// Note that the algo explores the clause to its greatest depth. That's
// OK, because typical clauses are never very deep.
//
// A variant of this algo could incorporate the Attentional focus
// into the "thinnest" calculation, so that only high-AF atoms are
// considered.
//
// Note that the size of the incoming set really is a better measure,
// and not the depth.  So, for example, if "item" has a huge incoming
// set, but "blah" does not, then "blah" is a much better place to
// start.
//
// size_t& depth will be set to the depth of the thinnest constant found.
// Handle& start will be set to the link containing that constant.
// size_t& width will be set to the incoming-set size of the thinnest
//               constant found.
// The returned value will be the constant at which to start the search.
// If no constant is found, then the returned value is the undefnied
// handle.
//
Handle
InitiateSearchCB::find_starter(const Handle& h, size_t& depth,
                               Handle& start, size_t& width)
{
	// If its a node, then we are done. Don't modify either depth or
	// start.
	Type t = h->getType();
	if (_classserver.isNode(t)) {
		if (t != VARIABLE_NODE) {
			width = h->getIncomingSetSize();
			return h;
		}
		return Handle::UNDEFINED;
	}

	// Ignore all ChoiceLink's. Picking a starter inside one of these
	// will almost surely be disconnected from the rest of the graph.
	if (CHOICE_LINK == t)
		return Handle::UNDEFINED;

	// Ignore all dynamically-evaluatable links up front.
	if (_dynamic->find(h) != _dynamic->end())
		return Handle::UNDEFINED;

	// Iterate over all the handles in the outgoing set.
	// Find the deepest one that contains a constant, and start
	// the search there.  If there are two at the same depth,
	// then start with the skinnier one.
	size_t deepest = depth;
	start = Handle::UNDEFINED;
	Handle hdeepest(Handle::UNDEFINED);
	size_t thinnest = SIZE_MAX;

	// If there is a ComposeLink, then search it's definition instead.
	// but do this only at depth zero, so as to get us started; otherwise
	// we risk infinite descent if the compose is recursive.
	LinkPtr ll(LinkCast(h));
	if (0 == depth and BETA_REDEX == ll->getType())
	{
		BetaRedexPtr cpl(BetaRedexCast(ll));
		ll = LinkCast(cpl->beta_reduce());
	}
	for (Handle hunt : ll->getOutgoingSet())
	{
		size_t brdepth = depth + 1;
		size_t brwid = SIZE_MAX;
		Handle sbr(h);

		// Blow past the QuoteLinks, since they just screw up the search start.
		if (QUOTE_LINK == hunt->getType())
			hunt = LinkCast(hunt)->getOutgoingAtom(0);

		Handle s(find_starter(hunt, brdepth, sbr, brwid));

		if (s != Handle::UNDEFINED
		    and (brwid < thinnest
		         or (brwid == thinnest and deepest < brdepth)))
		{
			deepest = brdepth;
			hdeepest = s;
			start = sbr;
			thinnest = brwid;
		}

	}
	depth = deepest;
	width = thinnest;
	return hdeepest;
}

/* ======================================================== */
/**
 * Iterate over all the clauses, to find the "thinnest" one.
 * Skip any/all evaluatable clauses, as thes typically do not
 * exist in the atomspace, anyway.
 */
Handle InitiateSearchCB::find_thinnest(const HandleSeq& clauses,
                                       const std::set<Handle>& evl,
                                       Handle& starter_term,
                                       size_t& bestclause)
{
	size_t thinnest = SIZE_MAX;
	size_t deepest = 0;
	bestclause = 0;
	Handle best_start(Handle::UNDEFINED);
	starter_term = Handle::UNDEFINED;

	size_t nc = clauses.size();
	for (size_t i=0; i < nc; i++)
	{
		// Cannot start with an evaluatable clause!
		if (0 < evl.count(clauses[i])) continue;
		Handle h(clauses[i]);
		size_t depth = 0;
		size_t width = SIZE_MAX;
		Handle term(Handle::UNDEFINED);
		Handle start(find_starter(h, depth, term, width));
		if (start != Handle::UNDEFINED
		    and (width < thinnest
		         or (width == thinnest and depth > deepest)))
		{
			thinnest = width;
			deepest = depth;
			bestclause = i;
			best_start = start;
			starter_term = term;
		}
	}

    return best_start;
}

/* ======================================================== */
/**
 * Given a set of clauses, find a neighborhood to search, and perform
 * the search. A `neighborhood` is defined as all of the atoms that
 * can be reached from a given (non-variable) atom, by following either
 * it's incoming or its outgoing set.
 *
 * A neighborhood search is guaranteed to find all possible groundings
 * for the set of clauses. The reason for this is that, given a
 * non-variable atom in the pattern, any possible grounding of that
 * pattern must contain that atom, out of necessity. Thus, any possible
 * grounding must be contained in that neighborhood.  It is sufficient
 * to walk that graph until a suitable grounding is encountered.
 *
 * The return value is true if a grounding was found, else it returns
 * false. That is, this return value works just like all the other
 * satisfiability callbacks.  The flag '_search_fail' is set to true
 * if the search was not performed, due to a failure to find a sutiable
 * starting point.
 */
bool InitiateSearchCB::neighbor_search(PatternMatchEngine *pme)
{
	// Sometimes, the number of mandatory clauses can be zero...
	// We still want to search, though.
	const HandleSeq& clauses =
		(0 < _pattern->mandatory.size()) ?
			 _pattern->mandatory : _pattern->cnf_clauses;

	// In principle, we could start our search at some node, any node,
	// that is not a variable. In practice, the search begins by
	// iterating over the incoming set of the node, and so, if it is
	// large, a huge amount of effort might be wasted exploring
	// dead-ends.  Thus, it pays off to start the search on the
	// node with the smallest ("narrowest" or "thinnest") incoming set
	// possible.  Thus, we look at all the clauses, to find the
	// "thinnest" one.
	//
	// Note also: the user is allowed to specify patterns that have
	// no constants in them at all.  In this case, the search is
	// performed by looping over all links of the given types.
	size_t bestclause;
	Handle best_start = find_thinnest(clauses, _pattern->evaluatable_holders,
	                                  _starter_term, bestclause);

	// Cannot find a starting point! This can happen if all of the
	// clauses contain nothing but variables, or if all of the
	// clauses are evaluatable(!) Somewhat unusual, but it can
	// happen.  In this case, we need some other, alternative search
	// strategy.
	if (Handle::UNDEFINED == best_start)
	{
		_search_fail = true;
		return false;
	}

	_root = clauses[bestclause];
	dbgprt("Search start node: %s\n", best_start->toShortString().c_str());
	dbgprt("Start term is: %s\n", _starter_term == Handle::UNDEFINED ?
	       "UNDEFINED" : _starter_term->toShortString().c_str());
	dbgprt("Root clause is: %s\n", _root->toShortString().c_str());

	// This should be calling the over-loaded virtual method
	// get_incoming_set(), so that, e.g. it gets sorted by attentional
	// focus in the AttentionalFocusCB class...
	IncomingSet iset = get_incoming_set(best_start);
	size_t sz = iset.size();
	for (size_t i = 0; i < sz; i++)
	{
		Handle h(iset[i]);
		dbgprt("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n");
		dbgprt("Loop candidate (%lu/%lu):\n%s\n", i+1, sz,
		       h->toShortString().c_str());
		bool found = pme->explore_neighborhood(_root, _starter_term, h);

		// Terminate search if satisfied.
		if (found) return true;
	}

	// If we are here, we have searched the entire neighborhood, and
	// no satisfiable groundings were found.
	return false;
}

/* ======================================================== */
/**
 * Search for solutions/groundings over all of the AtomSpace, using
 * the standard, canonical assumptions about the structure of the search
 * pattern.  Here, the "standard, canonical" assumptions are that the
 * pattern consists of clauses that contain VariableNodes in them, with
 * the VariableNodes interpreted in the "standard, canonical" way:
 * namely, that these are the atoms that are to be grounded, as normally
 * described elsewhere in the documentation.  In such a case, a full and
 * complete search for any/all possible groundings is performed; if
 * there are groundings, they are guaranteed to be found; if there are
 * none, then it is guaranteed that this will also be correctly
 * reported. For certain, highly unusual (but still canonical) search
 * patterns, the same grounding may be reported more than once; grep for
 * notes pertaining to the ChoiceLink, and the ArcanaUTest for details.
 * Otherwise, all possible groundings are guaranteed to be returned
 * exactly once.
 *
 * We emphasize "standard, canonical" here, for a reason: the pattern
 * engine is capable of doing many strange, weird things, depending on
 * how the callbacks are designed to work.  For those other
 * applications, it is possible or likely that this method will fail to
 * traverse the "interesting" parts of the atomspace: non-standard
 * callbacks may also need a non-standard search strategy.
 *
 * Now, some notes on the strategy employed here, and how non-canonical
 * callbacks might affect it:
 *
 * 1) Search will begin at the first non-variable node in the "thinnest"
 *    clause.  The thinnest clause is chosen, so as to improve performance;
 *    but this has no effect on the thoroughness of the search.  The search
 *    will proceed by exploring the entire incoming-set for this node.
 *
 *    This is ideal, when the node_match() callback accepts a match only
 *    when the pattern and suggested nodes are identical (i.e. are
 *    exactly the same atom).  If the node_match() callback is willing to
 *    accept a broader range of node matches, then other possible
 *    solutions might be missed. Just how to fix this depends sharpely
 *    on what node_match() is willing to accept as a match.
 *
 *    Anyway, this seems like a very reasonable limitation: if you
 *    really want a lenient node_match(), then use variables instead.
 *    Don't overload node-match with something weird, and you should be
 *    OK.  Otherwise, you'll have to implement your own initiate_search()
 *    callback.
 *
 * 2) If the clauses consist entirely of variables, i.e. if there is not
 *    even one single non-variable node in the pattern, then a search is
 *    driven by looking for all links that are of the same type as one
 *    of the links in one of the clauses.
 *
 *    If the link_match() callback is willing to accept a broader range
 *    of types, then this search method may fail to find some possible
 *    patterns.
 *
 *    Lets start by noting that this situation is very rare: most
 *    patterns will not consist entirely if Links and VariableNodes.
 *    Almost surely, most reasonable people will have at least one
 *    non-variable node in the pattern. So the disucssion below almost
 *    surely does not apply.
 *
 *    But if yhou really want this, there are several possible remedies.
 *    One is to modify the link_type_search() callback to try each
 *    possible link type that is considered bo be equivalent by
 *    link_match(). Another alternative is to just leave the
 *    link_match() callback alone, and use variables for links, instead.
 *    This is probably the best strategy, because then the fairly
 *    standard reasoning can be used when thinking about the problem.
 *    Of course, you can always write your own initiate_search() callback.
 *
 * If the constraint 1) can be met, (which is always the case for
 * "standard, canonical" searches, then the pattern match should be
 * quite rapid.  Incoming sets tend to be small; in addition, the
 * implemnentation here picks the smallest, "tinnest" incoming set to
 * explore.
 *
 * The default implementation of node_match() and link_match() in this
 * class does satisfy both 1) and 2), so this algo will work correctly,
 * if these two methods are not overloaded with more callbacks that are
 * lenient about matching.
 *
 * If you overload node_match(), and do so in a way that breaks
 * assumption 1), then you will scratch your head, thinking
 * "why did my search fail to find this obvious solution?" The answer
 * will be for you to create a new search algo, in a new class, that
 * overloads this one, and does what you want it to.  This class should
 * probably *not* be modified, since it is quite efficient for the
 * "standard, canonical" case.
 */
bool InitiateSearchCB::initiate_search(PatternMatchEngine *pme)
{
	return disjunct_search(pme);
}

void InitiateSearchCB::set_pattern(const Variables& vars,
                                   const Pattern& pat)
{
	_search_fail = false;
	_variables = &vars;
	_pattern = &pat;
	_type_restrictions = &vars.typemap;
	_dynamic = &pat.evaluatable_terms;
}

/* ======================================================== */
/**
 * This callback implements the handling of the special case where the
 * pattern consists of a single clause, at the top of which there is an
 * ChoiceLink. In this situation, one effectively has multiple, unrelated
 * grounding problems at hand, and they need to be treated as such.
 *
 * The core issue here is that, from the point of view of satisfiability,
 * each subgraph that occurs inside an ChoiceLink might be grounded by a
 * graph that is disconnected from the other subgraphs. There is no
 * a-priori way of knowing whether the groundings might be connected,
 * and thus, the worst-case must be assumed: each subgraph that occurs
 * inside an ChoiceLink must be considered to be a unique, independent
 * graph, which must be assumed to be disconnected from each of the
 * other subgraphs (even though they "accidentally" share a common
 * variable name).
 *
 * This is best understood through an example. Consider the clause
 *
 *   ChoiceLink
 *       ListLink
 *           ConceptNode Hunt
 *           VariableNode $X
 *       ListLink
 *           VariableNode $X
 *           ConceptNode Zebra
 *
 * Suppose that the Universe over which this is being grounded consists
 * of only two clauses:
 *
 *   ListLink
 *       ConceptNode Hunt
 *       ConceptNode RedOctober
 *
 *   ListLink
 *       ConceptNode IceStation
 *       ConceptNode Zebra
 *
 * Suppose that the search for a grounding is begun at `Hunt`. Then, the
 * `RedOctober` is found, so $X is grounded by `RedOctober`.  From here,
 * it is impossible to walk the graph in a connected manner to find the
 * alternative grounding: `IceStation`.  To find `IceStation`, a second
 * search needs to be launched, starting at `Zebra`.
 *
 * Since the pattern matcher is only able to walk over connected
 * graphs, it must be assumed a-priori that each subgraph in an ChoiceLink
 * is disconnected from the others. The only way that these two sub
 * graphs might prove to be connected is if the variable $X is used in
 * some other clause, thus establishing connectivity from that clause to
 * the subgraphs of the ChoiceLink.
 *
 * The practical side-effect, here, with regards to satisfcation, is
 * this: if both groundings are to be found in the above example, then
 * two efforts must be made: One effort, with the initial grounding
 * starting at `Hunt`, and a second, starting at `Zebra`.  So... that
 * is what we do here, with the ChoiceLink loop.
 */
bool InitiateSearchCB::disjunct_search(PatternMatchEngine *pme)
{
	const HandleSeq& clauses = _pattern->mandatory;

	if (1 == clauses.size() and clauses[0]->getType() == CHOICE_LINK)
	{
		const Pattern* porig = _pattern;
		Pattern pcpy = *_pattern;
		LinkPtr orl(LinkCast(clauses[0]));
		const HandleSeq& oset = orl->getOutgoingSet();
		for (const Handle& h : oset)
		{
			HandleSeq hs;
			hs.push_back(h);
			pcpy.mandatory = hs;
			_pattern = &pcpy;
			bool found = disjunct_search(pme);
			_pattern = porig;
			if (found) return true;
		}
		if (not _search_fail) return false;
	}

	dbgprt("Attempt to use node-neighbor search\n");
	_search_fail = false;
	bool found = neighbor_search(pme);
	if (found) return true;
	if (not _search_fail) return false;

	// If we are here, then we could not find a clause at which to
	// start, which can happen if the clauses consist entirely of
	// variables! Which can happen (there is a unit test for this,
	// the LoopUTest), and so instead, we search based on the link
	// types that occur in the atomspace.
	dbgprt("Cannot use node-neighbor search, use link-type search\n");
	_search_fail = false;
	found = link_type_search(pme);
	if (found) return true;
	if (not _search_fail) return false;

	// The URE Reasoning case: if we found nothing, then there are no
	// links!  Ergo, every clause must be a lone variable, all by
	// itself. This is how some URE rules may start: the specify a single
	// variable, all by itself, and set some type restrictions on it,
	// and that's all. We deal with this in the variable_search()
	// method.
	dbgprt("Cannot use link-type search, use variable-type search\n");
	_search_fail = false;
	found = variable_search(pme);
	return found;
}

/* ======================================================== */
/**
 * Find the rarest link type contained in the clause, or one
 * of its subclauses. Of course, QuoteLinks, and anything under
 * a Quotelink, must be ignored.
 */
void InitiateSearchCB::find_rarest(const Handle& clause,
                                   Handle& rarest,
                                   size_t& count)
{
	Type t = clause->getType();
	if (QUOTE_LINK == t) return;
	if (CHOICE_LINK == t) return;

	LinkPtr lll(LinkCast(clause));
	if (NULL == lll) return;

	size_t num = (size_t) _as->get_num_atoms_of_type(t);
	if (num < count)
	{
		count = num;
		rarest = clause;
	}

	const HandleSeq& oset = lll->getOutgoingSet();
	for (const Handle& h : oset)
		find_rarest(h, rarest, count);
}

/* ======================================================== */
/**
 * Initiate a search by looping over all Links of the same type as one
 * of the links in the set of clauses.  This attempts to pick the link
 * type which has the smallest number of atoms of that type in the
 * AtomSpace.
 */
bool InitiateSearchCB::link_type_search(PatternMatchEngine *pme)
{
	const HandleSeq& clauses = _pattern->mandatory;

	_search_fail = false;
	_root = Handle::UNDEFINED;
	_starter_term = Handle::UNDEFINED;
	size_t count = SIZE_MAX;

	for (const Handle& cl: clauses)
	{
		// Evaluatables don't exist in the atomspace, in general.
		// Cannot start a search with them.
		if (0 < _pattern->evaluatable_holders.count(cl)) continue;
		size_t prev = count;
		find_rarest(cl, _starter_term, count);
		if (count < prev)
		{
			prev = count;
			_root = cl;
		}
	}

	// The URE Reasoning case: if we found nothing, then there are no
	// links!  Ergo, every clause must be a lone variable, all by
	// itself. This is how some URE rules may start: the specify a single
	// variable, all by itself, and set some type restrictions on it,
	// and that's all. We deal with this in the variable_search()
	// method.
	if (Handle::UNDEFINED == _root)
	{
		_search_fail = true;
		return false;
	}

	dbgprt("Start clause is: %s\n", _root->toShortString().c_str());
	dbgprt("Start term is: %s\n", _starter_term->toShortString().c_str());

	// Get type of the rarest link
	Type ptype = _starter_term->getType();

	HandleSeq handle_set;
	_as->get_handles_by_type(handle_set, ptype);

#ifdef DEBUG
	size_t i = 0;
#endif
	for (const Handle& h : handle_set)
	{
		dbgprt("yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy\n");
		dbgprt("Loop candidate (%lu/%lu):\n%s\n", ++i, handle_set.size(),
		       h->toShortString().c_str());
		bool found = pme->explore_neighborhood(_root, _starter_term, h);
		if (found) return true;
	}
	return false;
}

/* ======================================================== */
/**
 * Initiate a search by looping over all atoms of the allowed
 * variable types (as set with the set_type_testrictions() method).
 * This assumes that the varset contains the variables to be searched
 * over, and that the type restrictins are set up approrpriately.
 *
 * If the varset is empty, or if there are no variables, then the
 * entire atomspace will be searched.  Depending on the pattern,
 * many, many duplicates might be reported. If you are not using
 * variables, then you probably don't want to use this metod, either;
 * you should create somethnig more clever.
 */
bool InitiateSearchCB::variable_search(PatternMatchEngine *pme)
{
	const HandleSeq& clauses = _pattern->mandatory;

	// Find the rarest variable type;
	size_t count = SIZE_MAX;
	Type ptype = ATOM;

	dbgprt("varset size = %lu\n", _variables->varset.size());
	_root = Handle::UNDEFINED;
	_starter_term = Handle::UNDEFINED;
	for (const Handle& var: _variables->varset)
	{
		dbgprt("Examine variable %s\n", var->toShortString().c_str());
		auto tit = _type_restrictions->find(var);
		if (_type_restrictions->end() == tit) continue;
		const std::set<Type>& typeset = tit->second;
		dbgprt("Type-restictions set size = %lu\n", typeset.size());
		for (Type t : typeset)
		{
			size_t num = (size_t) _as->get_num_atoms_of_type(t);
			dbgprt("Type = %s has %lu atoms in the atomspace\n",
			       _classserver.getTypeName(t).c_str(), num);
			if (0 < num and num < count)
			{
				for (const Handle& cl : clauses)
				{
					// Evaluatables dont' exist in the atomspace, in general.
					// Cannot start a search with them.
					if (0 < _pattern->evaluatable_holders.count(cl)) continue;
					FindAtoms fa(var);
					fa.search_set(cl);
					if (cl == var)
					{
						_root = cl;
						_starter_term = cl;
						count = num;
						ptype = t;
						dbgprt("New minimum count of %lu\n", count);
						break;
					}
					if (0 < fa.least_holders.size())
					{
						_root = cl;
						_starter_term = *fa.least_holders.begin();
						count = num;
						ptype = t;
						dbgprt("New minimum count of %lu (nonroot)\n", count);
						break;
					}
				}
			}
		}
	}

	// There were no type restrictions!
	if (Handle::UNDEFINED == _root)
	{
		dbgprt("There were no type restrictions! That must be wrong!\n");
		if (0 == clauses.size())
		{
			// This is kind-of weird, it can happen if all clauses
			// are optional.
			_search_fail = true;
			return false;
		}
		_root = _starter_term = clauses[0];
	}

	HandleSeq handle_set;
	if (ptype == ATOM)
		_as->get_handles_by_type(handle_set, ptype, true);
	else
		_as->get_handles_by_type(handle_set, ptype);

	dbgprt("Atomspace reported %lu atoms\n", handle_set.size());

#ifdef DEBUG
	size_t i = 0;
#endif
	for (const Handle& h : handle_set)
	{
		dbgprt("zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz\n");
		dbgprt("Loop candidate (%lu/%lu):\n%s\n", ++i, handle_set.size(),
		       h->toShortString().c_str());
		bool found = pme->explore_neighborhood(_root, _starter_term, h);
		if (found) return true;
	}

	return false;
}

/* ===================== END OF FILE ===================== */
