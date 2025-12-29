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

#include <QFrame>
#include <QLineEdit>
#include <QComboBox>
#include <QListWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QTimer>

#include <memory>

#include "source-item.hpp"

class SourceSearchDock : public QFrame {
	Q_OBJECT

public:
	explicit SourceSearchDock(QWidget *parent = nullptr);
	~SourceSearchDock();

	// Initialize after OBS is fully loaded
	void Initialize();

	// Cleanup before scene collection changes
	void Cleanup();

	// Focus the search box (for hotkey)
	void FocusSearchBox();

private slots:
	void OnSearchTextChanged(const QString &text);
	void OnSearchScopeChanged(int index);
	void OnTypeFilterChanged(int index);
	void OnResultDoubleClicked(QListWidgetItem *item);
	void OnResultContextMenu(const QPoint &pos);
	void PerformSearch();
	void OnSourcesChanged();

private:
	// Build the UI
	void SetupUI();

	// Update the type filter dropdown
	void UpdateTypeFilter();

	// Update search results
	void UpdateResults();

	// Open source properties
	void OpenSourceProperties(SourceItem *item);

	// Open source filters
	void OpenSourceFilters(SourceItem *item);

	// Signal handlers
	static void OnSourceCreate(void *data, calldata_t *params);
	static void OnSourceDestroy(void *data, calldata_t *params);
	static void OnSourceRename(void *data, calldata_t *params);

	// Connect/disconnect signal handlers
	void ConnectSignals();
	void DisconnectSignals();

	// UI elements
	QVBoxLayout *mainLayout;
	QLineEdit *searchBox;
	QComboBox *searchScope;
	QComboBox *typeFilter;
	QListWidget *resultsList;
	QLabel *statusLabel;

	// Source collection
	std::unique_ptr<SourceCollection> sourceCollection;

	// Current search parameters
	QString currentSearchText;
	QString currentSearchScope;
	QString currentTypeFilter;

	// Debounce timer for search
	QTimer *searchTimer;

	// Debounce timer for source changes (avoid refresh spam during startup)
	QTimer *refreshTimer;

	// Signal connection state
	bool signalsConnected;

	// Lazy initialization - only load sources when dock is first shown
	bool initialized;

protected:
	void showEvent(QShowEvent *event) override;
};
