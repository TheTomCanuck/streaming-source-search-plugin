/*
 * OBS Source Search Plugin
 * Copyright (C) 2024
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "source-item.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

// Known source type display names (fallback if OBS doesn't provide one)
const std::map<std::string, std::string> SOURCE_TYPE_NAMES = {
	{"scene", "Scene"},
	{"group", "Group"},
	{"image_source", "Image"},
	{"color_source", "Color Source"},
	{"color_source_v3", "Color Source v3"},
	{"slideshow", "Image Slide Show"},
	{"browser_source", "Browser"},
	{"ffmpeg_source", "Media Source"},
	{"vlc_source", "VLC Video Source"},
	{"text_gdiplus", "Text (GDI+)"},
	{"text_gdiplus_v2", "Text (GDI+) v2"},
	{"text_gdiplus_v3", "Text (GDI+) v3"},
	{"text_ft2_source", "Text (FreeType 2)"},
	{"text_ft2_source_v2", "Text (FreeType 2) v2"},
	{"monitor_capture", "Display Capture"},
	{"window_capture", "Window Capture"},
	{"game_capture", "Game Capture"},
	{"dshow_input", "Video Capture Device"},
	{"wasapi_input_capture", "Audio Input Capture"},
	{"wasapi_output_capture", "Audio Output Capture"},
	{"pulse_input_capture", "Audio Input Capture (PulseAudio)"},
	{"pulse_output_capture", "Audio Output Capture (PulseAudio)"},
	{"ndi_source", "NDI Source"},
	{"obs_stinger_transition", "Stinger"},
};

// Helper: case-insensitive string contains
static bool ContainsCaseInsensitive(const std::string &haystack, const std::string &needle)
{
	if (needle.empty())
		return true;
	if (haystack.empty())
		return false;

	auto it = std::search(
		haystack.begin(), haystack.end(),
		needle.begin(), needle.end(),
		[](char a, char b) {
			return std::tolower(static_cast<unsigned char>(a)) ==
			       std::tolower(static_cast<unsigned char>(b));
		});

	return it != haystack.end();
}

// SourceItem implementation

SourceItem::SourceItem(obs_source_t *source)
	: weakSource(nullptr),
	  sourceClass(SourceClass::Source)
{
	if (!source)
		return;

	weakSource = obs_source_get_weak_source(source);

	// Determine source class
	obs_source_type type = obs_source_get_type(source);
	if (type == OBS_SOURCE_TYPE_FILTER) {
		sourceClass = SourceClass::Filter;
	} else if (obs_source_is_scene(source)) {
		sourceClass = SourceClass::Scene;
	} else if (obs_source_is_group(source)) {
		sourceClass = SourceClass::Group;
	}

	// Cache the type ID
	const char *typeId = obs_source_get_id(source);
	if (typeId) {
		cachedTypeId = typeId;
	}
}

SourceItem::~SourceItem()
{
	if (weakSource) {
		obs_weak_source_release(weakSource);
	}
}

std::string SourceItem::GetName() const
{
	obs_source_t *source = GetSource();
	if (!source)
		return "";

	const char *name = obs_source_get_name(source);
	std::string result = name ? name : "";
	obs_source_release(source);
	return result;
}

std::string SourceItem::GetDisplayName() const
{
	std::string name = GetName();
	if (name.empty())
		return name;

	// Add (H) or (V) prefix for scenes
	if (sourceClass == SourceClass::Scene) {
		std::string prefix = IsVerticalCanvas() ? "(V) " : "(H) ";
		return prefix + name;
	}

	return name;
}

std::string SourceItem::GetUUID() const
{
	obs_source_t *source = GetSource();
	if (!source)
		return "";

	const char *uuid = obs_source_get_uuid(source);
	std::string result = uuid ? uuid : "";
	obs_source_release(source);
	return result;
}

std::string SourceItem::GetTypeId() const
{
	return cachedTypeId;
}

std::string SourceItem::GetTypeDisplayName() const
{
	return ::GetTypeDisplayName(cachedTypeId);
}

obs_source_t *SourceItem::GetSource() const
{
	if (!weakSource)
		return nullptr;
	return obs_weak_source_get_source(weakSource);
}

void SourceItem::AddParentScene(const std::string &sceneName)
{
	parentScenes.insert(sceneName);
}

bool SourceItem::IsVerticalCanvas() const
{
	// Only scenes can be on vertical canvas
	if (sourceClass != SourceClass::Scene)
		return false;

	obs_source_t *source = GetSource();
	if (!source)
		return false;

	// Get the main scene list
	struct obs_frontend_source_list mainScenes = {};
	obs_frontend_get_scenes(&mainScenes);

	// Check if this scene is in the main list
	bool isMainScene = false;
	for (size_t i = 0; i < mainScenes.sources.num; i++) {
		if (mainScenes.sources.array[i] == source) {
			isMainScene = true;
			break;
		}
	}

	obs_frontend_source_list_free(&mainScenes);
	obs_source_release(source);

	// If it's a scene but not in main scenes, it's from vertical canvas
	return !isMainScene;
}

bool SourceItem::MatchesSearch(const std::string &searchText) const
{
	if (searchText.empty())
		return true;

	// Match against name only
	if (ContainsCaseInsensitive(GetName(), searchText))
		return true;

	return false;
}

bool SourceItem::MatchesType(const std::string &typeFilter) const
{
	if (typeFilter.empty() || typeFilter == "all")
		return true;

	return cachedTypeId == typeFilter;
}

bool SourceItem::IsValid() const
{
	obs_source_t *source = GetSource();
	if (source) {
		obs_source_release(source);
		return true;
	}
	return false;
}

// SourceCollection implementation

SourceCollection::SourceCollection() {}

SourceCollection::~SourceCollection()
{
	Clear();
}

void SourceCollection::Clear()
{
	sources.clear();
	sourcesByUUID.clear();
	discoveredTypes.clear();
}

void SourceCollection::Refresh()
{
	Clear();

	// Enumerate all sources (this includes VC scenes but NOT filters)
	obs_enum_all_sources(EnumAllSourcesCallback, this);

	// Enumerate filters on each source and link them
	LinkFilters();

	// Link scene items to their parent scenes
	LinkSceneItems();

	blog(LOG_INFO, "[Source Search] Refreshed: found %zu sources, %zu types",
	     sources.size(), discoveredTypes.size());
}

bool SourceCollection::EnumAllSourcesCallback(void *param, obs_source_t *source)
{
	SourceCollection *self = static_cast<SourceCollection *>(param);
	self->AddSource(source);
	return true;
}

void SourceCollection::AddSource(obs_source_t *source)
{
	if (!source)
		return;

	// Skip filters - they'll be added separately with parent info
	// Skip transitions - they're internal OBS sources
	obs_source_type type = obs_source_get_type(source);
	if (type == OBS_SOURCE_TYPE_FILTER || type == OBS_SOURCE_TYPE_TRANSITION)
		return;

	// Skip sources without names
	const char *name = obs_source_get_name(source);
	if (!name || strlen(name) == 0)
		return;

	// Get type ID for filtering
	const char *typeId = obs_source_get_id(source);
	if (!typeId)
		return;

	// Skip internal OBS sources
	if (strcmp(typeId, "audio_monitor") == 0) {
		blog(LOG_INFO, "[Source Search] Skipping audio_monitor: %s", name);
		return;
	}

	// Skip internal wrapper sources (from plugins like Vertical Canvas)
	if (strstr(typeId, "_wrapper_") != nullptr)
		return;

	// Skip stinger transition media sources (they have "(Stinger)" suffix)
	if (strstr(name, "(Stinger)") != nullptr)
		return;

	// Skip audio line sources (internal)
	if (strcmp(typeId, "audio_line") == 0)
		return;

	// Check for duplicates by UUID
	const char *uuid = obs_source_get_uuid(source);
	if (uuid && sourcesByUUID.find(uuid) != sourcesByUUID.end()) {
		return;
	}

	// Create source item
	auto item = std::make_unique<SourceItem>(source);
	if (!item->IsValid())
		return;

	// Track discovered type
	std::string typeIdStr = typeId;
	if (discoveredTypes.find(typeIdStr) == discoveredTypes.end()) {
		discoveredTypes[typeIdStr] = GetTypeDisplayName(typeIdStr);
	}

	// Add to collections
	if (uuid) {
		sourcesByUUID[uuid] = item.get();
	}
	sources.push_back(std::move(item));
}

void SourceCollection::LinkFilters()
{
	// We need to enumerate filters on all sources we've collected
	// Make a copy of the source list since we'll be adding to it
	std::vector<SourceItem *> sourcesToEnumerate;
	for (auto &item : sources) {
		sourcesToEnumerate.push_back(item.get());
	}

	for (SourceItem *item : sourcesToEnumerate) {
		obs_source_t *source = item->GetSource();
		if (!source)
			continue;

		std::string parentName = item->GetName();

		// Enumerate filters on this source
		struct FilterEnumContext {
			SourceCollection *self;
			std::string parentName;
		};
		FilterEnumContext ctx = {this, parentName};

		obs_source_enum_filters(
			source,
			[](obs_source_t *, obs_source_t *filter, void *param) {
				FilterEnumContext *ctx = static_cast<FilterEnumContext *>(param);
				ctx->self->AddFilter(filter, ctx->parentName);
			},
			&ctx);

		obs_source_release(source);
	}
}

void SourceCollection::AddFilter(obs_source_t *filter, const std::string &parentName)
{
	if (!filter)
		return;

	const char *name = obs_source_get_name(filter);
	if (!name || strlen(name) == 0)
		return;

	const char *typeId = obs_source_get_id(filter);
	if (!typeId)
		return;

	// Skip internal filter types
	if (strcmp(typeId, "audio_monitor") == 0)
		return;

	// Check for duplicates by UUID
	const char *uuid = obs_source_get_uuid(filter);
	if (uuid && sourcesByUUID.find(uuid) != sourcesByUUID.end()) {
		return;
	}

	// Create filter item
	auto item = std::make_unique<SourceItem>(filter);
	if (!item->IsValid())
		return;

	// Set parent source name
	item->SetParentSourceName(parentName);

	// Track discovered type
	std::string typeIdStr = typeId;
	if (discoveredTypes.find(typeIdStr) == discoveredTypes.end()) {
		discoveredTypes[typeIdStr] = GetTypeDisplayName(typeIdStr);
	}

	// Add to collections
	if (uuid) {
		sourcesByUUID[uuid] = item.get();
	}
	sources.push_back(std::move(item));
}

void SourceCollection::LinkSceneItems()
{
	// For each scene (main and vertical), enumerate items and link them
	for (auto &item : sources) {
		if (!item->IsScene() && !item->IsGroup())
			continue;

		obs_source_t *source = item->GetSource();
		if (!source)
			continue;

		obs_scene_t *scene = obs_scene_from_source(source);
		if (!scene) {
			obs_source_release(source);
			continue;
		}

		std::string sceneName = item->GetName();

		// Enumerate scene items
		struct EnumContext {
			SourceCollection *self;
			std::string sceneName;
		};
		EnumContext ctx = {this, sceneName};

		obs_scene_enum_items(scene,
			[](obs_scene_t *, obs_sceneitem_t *sceneItem, void *param) {
				EnumContext *ctx = static_cast<EnumContext *>(param);

				obs_source_t *itemSource = obs_sceneitem_get_source(sceneItem);
				if (!itemSource)
					return true;

				// Link source to parent scene
				const char *uuid = obs_source_get_uuid(itemSource);
				if (uuid) {
					auto it = ctx->self->sourcesByUUID.find(uuid);
					if (it != ctx->self->sourcesByUUID.end()) {
						it->second->AddParentScene(ctx->sceneName);
					}
				}

				return true;
			},
			&ctx);

		obs_source_release(source);
	}
}

std::vector<SourceItem *> SourceCollection::Search(const std::string &searchText,
						    const std::string &typeFilter) const
{
	std::vector<SourceItem *> results;

	for (const auto &item : sources) {
		if (!item->IsValid())
			continue;

		if (!item->MatchesType(typeFilter))
			continue;

		if (!item->MatchesSearch(searchText))
			continue;

		results.push_back(item.get());
	}

	// Sort by name
	std::sort(results.begin(), results.end(),
		  [](const SourceItem *a, const SourceItem *b) {
			  return a->GetName() < b->GetName();
		  });

	return results;
}

// Utility function

std::string GetTypeDisplayName(const std::string &typeId)
{
	// Check our hardcoded map first
	auto it = SOURCE_TYPE_NAMES.find(typeId);
	if (it != SOURCE_TYPE_NAMES.end())
		return it->second;

	// Try to get from OBS
	const char *displayName = obs_source_get_display_name(typeId.c_str());
	if (displayName && strlen(displayName) > 0)
		return displayName;

	// Fallback to type ID
	return typeId;
}
