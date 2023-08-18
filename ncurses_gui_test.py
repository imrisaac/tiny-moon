import curses

def main(stdscr):
    # Enable mouse events
    curses.mousemask(1)
    
    # Use a color pair for selected item
    curses.start_color()
    curses.init_pair(1, curses.COLOR_BLACK, curses.COLOR_WHITE)
    
    menu_items = ["Option 1", "Option 2", "Exit"]
    current_item = 0

    while True:
        stdscr.clear()
        
        h, w = stdscr.getmaxyx()
        x = w // 2  # Center the menu on the screen

        # Display menu items
        for idx, item in enumerate(menu_items):
            y = h // 2 - len(menu_items) // 2 + idx
            if idx == current_item:
                stdscr.attron(curses.color_pair(1))
                stdscr.addstr(y, x - len(item) // 2, item)
                stdscr.attroff(curses.color_pair(1))
            else:
                stdscr.addstr(y, x - len(item) // 2, item)

        # Handle user input
        key = stdscr.getch()

        if key == curses.KEY_MOUSE:
            _, mx, my, _, _ = curses.getmouse()
            for idx, item in enumerate(menu_items):
                y = h // 2 - len(menu_items) // 2 + idx
                x_left = x - len(item) // 2
                x_right = x + len(item) // 2
                
                if my == y and x_left <= mx <= x_right:
                    current_item = idx
                    break

        # Handle selection
        if menu_items[current_item] == "Exit":
            break

    stdscr.refresh()

# Initialize curses
curses.wrapper(main)
