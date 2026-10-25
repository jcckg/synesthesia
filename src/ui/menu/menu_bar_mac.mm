#include "ui/menu/menu_bar.h"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <utility>
#include <vector>

@interface SynesthesiaMenuTarget : NSObject <NSMenuItemValidation, NSMenuDelegate> {
@public
    UI::Menu::MenuBar::Query query;
    std::vector<UI::Menu::Command> pendingCommands;
    NSMapTable<NSMenuItem*, NSMenuItem*>* subtitleItems;
}
- (void)performCommand:(NSMenuItem*)item;
- (void)updatePresentation:(NSMenuItem*)item state:(const UI::Menu::State&)state;
@end

@implementation SynesthesiaMenuTarget

- (BOOL)validateMenuItem:(NSMenuItem*)item {
    const auto state = query ? query(static_cast<UI::Menu::Command>(item.tag)) : UI::Menu::State{};
    [self updatePresentation:item state:state];
    return NSApp.modalWindow == nil && state.enabled;
}

- (void)updatePresentation:(NSMenuItem*)item state:(const UI::Menu::State&)state {
    if (!state.title.empty()) item.title = [NSString stringWithUTF8String:state.title.c_str()];
    item.state = state.checked ? NSControlStateValueOn : NSControlStateValueOff;
    NSString* subtitle = state.subtitle.empty() ? nil : [NSString stringWithUTF8String:state.subtitle.c_str()];
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 140400
    if (@available(macOS 14.4, *)) {
        item.subtitle = subtitle;
        return;
    }
#endif
    NSMenuItem* detail = [subtitleItems objectForKey:item];
    detail.title = subtitle != nil ? subtitle : @"";
    detail.hidden = subtitle == nil;
}

- (void)menuNeedsUpdate:(NSMenu*)menu {
    for (NSMenuItem* item in menu.itemArray) {
        if (item.target == self && item.action == @selector(performCommand:)) {
            item.enabled = [self validateMenuItem:item];
        }
    }
}

- (void)performCommand:(NSMenuItem*)item {
    if ([self validateMenuItem:item]) {
        pendingCommands.push_back(static_cast<UI::Menu::Command>(item.tag));
    }
}

@end

namespace UI::Menu {

struct MenuBar::Impl {
    SynesthesiaMenuTarget* target;
    NSMenu* mainMenu;
    NSMenu* previousHelpMenu;
    NSMenu* installedHelpMenu = nil;
    NSMutableArray<NSMenuItem*>* installedItems;
    Execute execute;

    Impl(std::span<const Item> menus, Query query, Execute action)
        : target([[SynesthesiaMenuTarget alloc] init]), mainMenu(NSApp.mainMenu),
          previousHelpMenu(NSApp.helpMenu),
          installedItems([[NSMutableArray alloc] init]), execute(std::move(action)) {
        target->query = std::move(query);
        target->subtitleItems = [NSMapTable weakToStrongObjectsMapTable];
        if (mainMenu == nil) {
            mainMenu = [[NSMenu alloc] initWithTitle:@""];
            NSApp.mainMenu = mainMenu;
        }

        NSInteger index = std::min<NSInteger>(1, mainMenu.numberOfItems);
        for (const Item& menu : menus) {
            NSMenuItem* item = makeItem(menu);
            if (menu.isHelpMenu) {
                [mainMenu addItem:item];
                installedHelpMenu = item.submenu;
                NSApp.helpMenu = installedHelpMenu;
            } else {
                [mainMenu insertItem:item atIndex:index++];
            }
            [installedItems addObject:item];
        }
    }

    ~Impl() {
        if (installedHelpMenu != nil && NSApp.helpMenu == installedHelpMenu) NSApp.helpMenu = previousHelpMenu;
        for (NSMenuItem* item in installedItems) {
            [mainMenu removeItem:item];
        }
    }

    NSMenuItem* makeItem(const Item& definition) {
        if (definition.title[0] == '\0') return [NSMenuItem separatorItem];

        NSString* title = [NSString stringWithUTF8String:definition.title];
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
            action:definition.command == Command::None ? nullptr : @selector(performCommand:)
            keyEquivalent:[NSString stringWithUTF8String:definition.shortcut]];
        if (definition.command != Command::None) {
            item.target = target;
            item.tag = static_cast<NSInteger>(definition.command);
            item.keyEquivalentModifierMask = NSEventModifierFlagCommand;
            if (definition.shiftShortcut) item.keyEquivalentModifierMask |= NSEventModifierFlagShift;
        }
        if (!definition.children.empty()) {
            NSMenu* submenu = [[NSMenu alloc] initWithTitle:title];
            submenu.delegate = target;
            for (const Item& child : definition.children) {
                NSMenuItem* childItem = makeItem(child);
                [submenu addItem:childItem];
                if (child.showsSubtitle) {
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 140400
                    if (@available(macOS 14.4, *)) continue;
#endif
                    NSMenuItem* detail = [[NSMenuItem alloc] initWithTitle:@"" action:nullptr keyEquivalent:@""];
                    detail.enabled = NO;
                    detail.hidden = YES;
                    detail.indentationLevel = 1;
                    [target->subtitleItems setObject:detail forKey:childItem];
                    [submenu addItem:detail];
                }
            }
            item.submenu = submenu;
        }
        return item;
    }

    void dispatchPendingCommands() {
        std::vector<Command> commands;
        commands.swap(target->pendingCommands);
        for (Command command : commands) {
            if (NSApp.modalWindow == nil && target->query && target->query(command).enabled && execute) execute(command);
        }
    }
};

MenuBar::MenuBar(std::span<const Item> menus, Query query, Execute execute)
    : impl_(std::make_unique<Impl>(menus, std::move(query), std::move(execute))) {}

MenuBar::~MenuBar() = default;

void MenuBar::dispatchPendingCommands() {
    if (impl_) impl_->dispatchPendingCommands();
}

void MenuBar::shutdown() {
    impl_.reset();
}

}
