/*
 * Copyright (C) 2026 VS:RP fork
 *
 * Drop-in replacement for the boost::bimap<> usage in chunk streaming. Keeps the two
 * access patterns the plugin needs, in a single ~80-line header, without the bimap
 * template machinery or the two-sided tuple comparators:
 *
 *   1. Ordered traversal by (priority DESC, distance ASC) — for "next best to stream
 *      in" (begin) and "worst candidate to evict" (rbegin).
 *   2. O(1) lookup by item id — for "is this already tracked?" and cross-player cleanup
 *      when an item is destroyed.
 *
 * The container stores entries keyed by a (priority, distance, id) triple in an
 * std::map, so iterator stability is preserved across mutations; a parallel
 * unordered_map<id, map::iterator> gives the by-id lookup.
 *
 * Licensed under Apache License, Version 2.0.
 */

#ifndef UTILITY_ORDERED_ITEM_SET_H
#define UTILITY_ORDERED_ITEM_SET_H

#include <map>
#include <unordered_map>

namespace Utility
{

template <typename Item>
class OrderedItemSet
{
public:
	struct Key
	{
		int priority;
		float distance;
		int id; // tiebreaker so no two entries collide

		friend bool operator<(const Key &a, const Key &b) noexcept
		{
			// PRIMARY: higher priority first (matches Item::LeftTupleCompare).
			if (a.priority != b.priority)
			{
				return a.priority > b.priority;
			}
			// SECONDARY: closer first.
			if (a.distance != b.distance)
			{
				return a.distance < b.distance;
			}
			// FINAL tiebreak: stable ordering by id.
			return a.id < b.id;
		}
	};

	using MapType = std::map<Key, Item>;
	using iterator = typename MapType::iterator;
	using const_iterator = typename MapType::const_iterator;
	using reverse_iterator = typename MapType::reverse_iterator;
	using const_reverse_iterator = typename MapType::const_reverse_iterator;

	// Insert or replace the entry for `id`. Old position (if any) is removed before the
	// new one is inserted so the tree keeps its sort order intact.
	void insertOrAssign(int priority, float distance, int id, const Item &item)
	{
		auto existing = byId_.find(id);
		if (existing != byId_.end())
		{
			sorted_.erase(existing->second);
			byId_.erase(existing);
		}
		auto [it, inserted] = sorted_.try_emplace(Key{priority, distance, id}, item);
		byId_.emplace(id, it);
	}

	// Erase by id (cross-player cleanup path). Returns true if something was removed.
	bool eraseById(int id)
	{
		auto it = byId_.find(id);
		if (it == byId_.end())
		{
			return false;
		}
		sorted_.erase(it->second);
		byId_.erase(it);
		return true;
	}

	// Erase during ordered iteration. Returns next iterator (like std::map::erase).
	iterator erase(iterator it)
	{
		byId_.erase(it->first.id);
		return sorted_.erase(it);
	}

	// Erase the worst candidate (back of the sorted view). No-op when empty.
	void popWorst()
	{
		if (sorted_.empty()) return;
		auto last = std::prev(sorted_.end());
		byId_.erase(last->first.id);
		sorted_.erase(last);
	}

	void clear() noexcept { sorted_.clear(); byId_.clear(); }
	bool empty() const noexcept { return sorted_.empty(); }
	std::size_t size() const noexcept { return sorted_.size(); }

	iterator begin() noexcept { return sorted_.begin(); }
	iterator end()   noexcept { return sorted_.end(); }
	const_iterator begin() const noexcept { return sorted_.begin(); }
	const_iterator end()   const noexcept { return sorted_.end(); }
	reverse_iterator rbegin() noexcept { return sorted_.rbegin(); }
	reverse_iterator rend()   noexcept { return sorted_.rend(); }

	bool contains(int id) const noexcept { return byId_.find(id) != byId_.end(); }

private:
	MapType sorted_;
	std::unordered_map<int, iterator> byId_;
};

} // namespace Utility

#endif
