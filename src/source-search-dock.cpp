/*
 * OBS Source Search Plugin
 * Copyright (C) 2024
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "source-search-dock.hpp"

#include <obs.h>
#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QHBoxLayout>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QShowEvent>

SourceSearchDock::SourceSearchDock(QWidget *parent)
	: QFrame(parent),
	  mainLayout(nullptr),
	  searchBox(nullptr),
	  searchScope(nullptr),
	  typeFilter(nullptr),
	  resultsList(nullptr),
	  statusLabel(nullptr),
	  sourceCollection(nullptr),
	  searchTimer(nullptr),
	  refreshTimer(nullptr),
	  signalsConnected(false),
	  initialized(false)
{
	SetupUI();

	sourceCollection = std::make_unique<SourceCollection>();

	// Setup search debounce timer
	searchTimer = new QTimer(this);
	searchTimer->setSingleShot(true);
	searchTimer->setInterval(150);  // 150ms debounce
	connect(searchTimer, &QTimer::timeout, this, &SourceSearchDock::PerformSearch);

	// Setup refresh debounce timer (avoid refresh spam during OBS startup)
	refreshTimer = new QTimer(this);
	refreshTimer->setSingleShot(true);
	refreshTimer->setInterval(500);  // 500ms debounce - coalesce rapid source changes
	connect(refreshTimer, &QTimer::timeout, this, &SourceSearchDock::OnSourcesChanged);
}

SourceSearchDock::~SourceSearchDock()
{
	Cleanup();
}

void SourceSearchDock::SetupUI()
{
	mainLayout = new QVBoxLayout(this);
	mainLayout->setContentsMargins(4, 4, 4, 4);
	mainLayout->setSpacing(4);

	// Search box row
	QHBoxLayout *searchRow = new QHBoxLayout();
	searchRow->setSpacing(4);

	searchBox = new QLineEdit(this);
	searchBox->setPlaceholderText(obs_module_text("SearchPlaceholder"));
	searchBox->setClearButtonEnabled(true);
	connect(searchBox, &QLineEdit::textChanged, this, &SourceSearchDock::OnSearchTextChanged);
	searchRow->addWidget(searchBox);

	mainLayout->addLayout(searchRow);

	// Search scope row
	QHBoxLayout *scopeRow = new QHBoxLayout();
	scopeRow->setSpacing(4);

	QLabel *scopeLabel = new QLabel(obs_module_text("Search"), this);
	scopeRow->addWidget(scopeLabel);

	searchScope = new QComboBox(this);
	searchScope->addItem(obs_module_text("Sources"), "sources");
	searchScope->addItem(obs_module_text("Filters"), "filters");
	searchScope->addItem(obs_module_text("All"), "all");
	searchScope->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	connect(searchScope, QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, &SourceSearchDock::OnSearchScopeChanged);
	scopeRow->addWidget(searchScope);
	currentSearchScope = "sources";

	mainLayout->addLayout(scopeRow);

	// Type filter row
	QHBoxLayout *filterRow = new QHBoxLayout();
	filterRow->setSpacing(4);

	QLabel *filterLabel = new QLabel(obs_module_text("Type"), this);
	filterRow->addWidget(filterLabel);

	typeFilter = new QComboBox(this);
	typeFilter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	connect(typeFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, &SourceSearchDock::OnTypeFilterChanged);
	filterRow->addWidget(typeFilter);

	mainLayout->addLayout(filterRow);

	// Results list
	resultsList = new QListWidget(this);
	resultsList->setSelectionMode(QAbstractItemView::SingleSelection);
	resultsList->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(resultsList, &QListWidget::itemDoubleClicked, this, &SourceSearchDock::OnResultDoubleClicked);
	connect(resultsList, &QListWidget::customContextMenuRequested, this, &SourceSearchDock::OnResultContextMenu);
	mainLayout->addWidget(resultsList, 1);

	// Status label
	statusLabel = new QLabel(this);
	statusLabel->setAlignment(Qt::AlignRight);
	mainLayout->addWidget(statusLabel);

	setLayout(mainLayout);
}

void SourceSearchDock::Initialize()
{
	// Don't do expensive initialization here - wait until dock is shown
	// Just connect signals so we know when to mark as needing refresh
	ConnectSignals();
}

void SourceSearchDock::showEvent(QShowEvent *event)
{
	QFrame::showEvent(event);

	// Lazy load: only refresh sources when dock is first shown
	if (!initialized) {
		initialized = true;
		sourceCollection->Refresh();
		UpdateTypeFilter();
		PerformSearch();
	}
}

void SourceSearchDock::Cleanup()
{
	DisconnectSignals();
	sourceCollection->Clear();
	resultsList->clear();
	typeFilter->clear();
}

void SourceSearchDock::FocusSearchBox()
{
	if (searchBox) {
		searchBox->setFocus();
		searchBox->selectAll();
	}
}

void SourceSearchDock::ConnectSignals()
{
	if (signalsConnected)
		return;

	signal_handler_t *handler = obs_get_signal_handler();
	if (handler) {
		signal_handler_connect(handler, "source_create", OnSourceCreate, this);
		signal_handler_connect(handler, "source_destroy", OnSourceDestroy, this);
		signal_handler_connect(handler, "source_rename", OnSourceRename, this);
		signalsConnected = true;
	}
}

void SourceSearchDock::DisconnectSignals()
{
	if (!signalsConnected)
		return;

	signal_handler_t *handler = obs_get_signal_handler();
	if (handler) {
		signal_handler_disconnect(handler, "source_create", OnSourceCreate, this);
		signal_handler_disconnect(handler, "source_destroy", OnSourceDestroy, this);
		signal_handler_disconnect(handler, "source_rename", OnSourceRename, this);
	}
	signalsConnected = false;
}

void SourceSearchDock::OnSourceCreate(void *data, calldata_t *params)
{
	UNUSED_PARAMETER(params);
	SourceSearchDock *self = static_cast<SourceSearchDock *>(data);

	// Only refresh if dock has been shown (initialized)
	// Use debounce timer to coalesce rapid changes during startup
	if (self->initialized) {
		QMetaObject::invokeMethod(self->refreshTimer, "start", Qt::QueuedConnection);
	}
}

void SourceSearchDock::OnSourceDestroy(void *data, calldata_t *params)
{
	UNUSED_PARAMETER(params);
	SourceSearchDock *self = static_cast<SourceSearchDock *>(data);

	if (self->initialized) {
		QMetaObject::invokeMethod(self->refreshTimer, "start", Qt::QueuedConnection);
	}
}

void SourceSearchDock::OnSourceRename(void *data, calldata_t *params)
{
	UNUSED_PARAMETER(params);
	SourceSearchDock *self = static_cast<SourceSearchDock *>(data);

	if (self->initialized) {
		QMetaObject::invokeMethod(self->refreshTimer, "start", Qt::QueuedConnection);
	}
}

void SourceSearchDock::OnSearchTextChanged(const QString &text)
{
	currentSearchText = text;
	searchTimer->start();  // Restart debounce timer
}

void SourceSearchDock::OnSearchScopeChanged(int index)
{
	if (index < 0)
		return;

	currentSearchScope = searchScope->itemData(index).toString();
	PerformSearch();
}

void SourceSearchDock::OnTypeFilterChanged(int index)
{
	if (index < 0)
		return;

	currentTypeFilter = typeFilter->itemData(index).toString();
	PerformSearch();
}

void SourceSearchDock::OnSourcesChanged()
{
	sourceCollection->Refresh();
	UpdateTypeFilter();
	PerformSearch();
}

void SourceSearchDock::PerformSearch()
{
	UpdateResults();
}

void SourceSearchDock::UpdateTypeFilter()
{
	// Save current selection
	QString currentSelection = currentTypeFilter;

	typeFilter->blockSignals(true);
	typeFilter->clear();

	// Add "All" option
	typeFilter->addItem(obs_module_text("AllTypes"), "all");

	// Add discovered types, sorted by display name
	const auto &types = sourceCollection->GetDiscoveredTypes();
	std::vector<std::pair<std::string, std::string>> sortedTypes(types.begin(), types.end());
	std::sort(sortedTypes.begin(), sortedTypes.end(),
		  [](const auto &a, const auto &b) {
			  return a.second < b.second;
		  });

	for (const auto &[typeId, displayName] : sortedTypes) {
		typeFilter->addItem(QString::fromStdString(displayName),
				    QString::fromStdString(typeId));
	}

	// Restore selection
	int index = typeFilter->findData(currentSelection);
	if (index >= 0) {
		typeFilter->setCurrentIndex(index);
	} else {
		typeFilter->setCurrentIndex(0);
		currentTypeFilter = "all";
	}

	typeFilter->blockSignals(false);
}

void SourceSearchDock::UpdateResults()
{
	resultsList->clear();

	std::string searchText = currentSearchText.toStdString();
	std::string typeFilterStr = currentTypeFilter.toStdString();
	std::string scopeStr = currentSearchScope.toStdString();

	auto results = sourceCollection->Search(searchText, typeFilterStr);

	int count = 0;
	for (SourceItem *item : results) {
		// Filter by search scope
		bool isFilter = item->IsFilter();
		if (scopeStr == "sources" && isFilter)
			continue;
		if (scopeStr == "filters" && !isFilter)
			continue;

		// Skip sources that aren't in any scene (internal OBS sources)
		// But keep scenes, groups, and filters
		if (!isFilter && !item->IsScene() && !item->IsGroup()) {
			const auto &parents = item->GetParentScenes();
			if (parents.empty())
				continue;
		}

		// Create display text
		QString displayText = QString::fromStdString(item->GetDisplayName());

		// Add type info
		displayText += QString(" [%1]").arg(QString::fromStdString(item->GetTypeDisplayName()));

		// For filters, show what source they're on
		if (isFilter) {
			std::string parentSource = item->GetParentSourceName();
			if (!parentSource.empty()) {
				displayText += QString(" on: %1").arg(QString::fromStdString(parentSource));
			}
		} else {
			// Add parent scenes for regular sources
			const auto &parents = item->GetParentScenes();
			if (!parents.empty()) {
				QStringList parentList;
				for (const auto &parent : parents) {
					parentList.append(QString::fromStdString(parent));
				}
				displayText += QString(" in: %1").arg(parentList.join(", "));
			}
		}

		QListWidgetItem *listItem = new QListWidgetItem(displayText, resultsList);
		listItem->setData(Qt::UserRole, QVariant::fromValue(static_cast<void *>(item)));
		count++;
	}

	// Update status
	statusLabel->setText(QString("%1 %2")
				.arg(count)
				.arg(obs_module_text("ResultsFound")));
}

void SourceSearchDock::OnResultDoubleClicked(QListWidgetItem *listItem)
{
	if (!listItem)
		return;

	SourceItem *item = static_cast<SourceItem *>(listItem->data(Qt::UserRole).value<void *>());
	if (!item)
		return;

	// Open properties on double-click
	OpenSourceProperties(item);
}

void SourceSearchDock::OnResultContextMenu(const QPoint &pos)
{
	QListWidgetItem *listItem = resultsList->itemAt(pos);
	if (!listItem)
		return;

	SourceItem *item = static_cast<SourceItem *>(listItem->data(Qt::UserRole).value<void *>());
	if (!item)
		return;

	QMenu menu(this);

	QAction *propsAction = menu.addAction(obs_module_text("OpenProperties"));
	connect(propsAction, &QAction::triggered, [this, item]() {
		OpenSourceProperties(item);
	});

	QAction *filtersAction = menu.addAction(obs_module_text("OpenFilters"));
	connect(filtersAction, &QAction::triggered, [this, item]() {
		OpenSourceFilters(item);
	});

	menu.exec(resultsList->mapToGlobal(pos));
}

void SourceSearchDock::OpenSourceProperties(SourceItem *item)
{
	if (!item)
		return;

	obs_source_t *source = item->GetSource();
	if (!source)
		return;

	obs_frontend_open_source_properties(source);
	obs_source_release(source);
}

void SourceSearchDock::OpenSourceFilters(SourceItem *item)
{
	if (!item)
		return;

	obs_source_t *source = item->GetSource();
	if (!source)
		return;

	obs_frontend_open_source_filters(source);
	obs_source_release(source);
}
