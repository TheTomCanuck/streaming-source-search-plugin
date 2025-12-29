/*
 * OBS Source Search Plugin
 * Copyright (C) 2024
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <obs.h>
#include <obs-frontend-api.h>

#include <string>
#include <vector>
#include <set>
#include <map>
#include <memory>

// Represents a source's class type
enum class SourceClass {
	Source,
	Scene,
	Group,
	Filter
};

// Display names for known source types
extern const std::map<std::string, std::string> SOURCE_TYPE_NAMES;

// Represents a single source item with metadata
class SourceItem {
public:
	explicit SourceItem(obs_source_t *source);
	~SourceItem();

	// Prevent copying
	SourceItem(const SourceItem &) = delete;
	SourceItem &operator=(const SourceItem &) = delete;

	// Getters
	std::string GetName() const;
	std::string GetDisplayName() const;  // With (H)/(V) prefix for scenes
	std::string GetUUID() const;
	std::string GetTypeId() const;
	std::string GetTypeDisplayName() const;
	SourceClass GetSourceClass() const { return sourceClass; }
	obs_source_t *GetSource() const;

	// Scene relationships
	void AddParentScene(const std::string &sceneName);
	const std::set<std::string> &GetParentScenes() const { return parentScenes; }

	// Filter parent source (for filters)
	void SetParentSourceName(const std::string &sourceName) { parentSourceName = sourceName; }
	std::string GetParentSourceName() const { return parentSourceName; }
	bool IsFilter() const { return sourceClass == SourceClass::Filter; }

	// Vertical Canvas detection
	bool IsVerticalCanvas() const;
	bool IsScene() const { return sourceClass == SourceClass::Scene; }
	bool IsGroup() const { return sourceClass == SourceClass::Group; }

	// Search matching
	bool MatchesSearch(const std::string &searchText) const;
	bool MatchesType(const std::string &typeFilter) const;

	// Validity check
	bool IsValid() const;

private:
	obs_weak_source_t *weakSource;
	SourceClass sourceClass;
	std::set<std::string> parentScenes;
	std::string parentSourceName;  // For filters: the source they're attached to

	// Cached type ID for filtering
	std::string cachedTypeId;
};

// Collection of all sources for searching
class SourceCollection {
public:
	SourceCollection();
	~SourceCollection();

	// Rebuild the entire source collection
	void Refresh();

	// Clear all sources
	void Clear();

	// Get all sources
	const std::vector<std::unique_ptr<SourceItem>> &GetSources() const { return sources; }

	// Get discovered source types for filter dropdown
	const std::map<std::string, std::string> &GetDiscoveredTypes() const { return discoveredTypes; }

	// Search and filter
	std::vector<SourceItem *> Search(const std::string &searchText, const std::string &typeFilter) const;

private:
	// Enumeration callbacks
	static bool EnumAllSourcesCallback(void *param, obs_source_t *source);
	static bool EnumSceneItemsCallback(obs_scene_t *scene, obs_sceneitem_t *item, void *param);

	// Add a source to collection
	void AddSource(obs_source_t *source);

	// Add a filter to collection with parent info
	void AddFilter(obs_source_t *filter, const std::string &parentName);

	// Enumerate and link filters to their parent sources
	void LinkFilters();

	// Link scene items to their parent scenes
	void LinkSceneItems();

	std::vector<std::unique_ptr<SourceItem>> sources;
	std::map<std::string, SourceItem *> sourcesByUUID;
	std::map<std::string, std::string> discoveredTypes;  // typeId -> displayName
};

// Utility function to get friendly type name
std::string GetTypeDisplayName(const std::string &typeId);
