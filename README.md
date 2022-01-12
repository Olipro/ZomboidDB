# ZomboidDB

This project consists of a library and patcher that results in file calls for your savegame(s) being transparently intercepted and redirected into a single, easy to manage SQLite database.

**This is currently just a technical preview, only 64-bit Windows is supported but Linux will be added as this might prove handy for server administrators.**

## Why?

Project Zomboid produces an inordinate number of `.bin` files which is a bit irritating when copying/deleting save folders when you want to backup your games.

Additionally, SQLite has support for "snapshots" - this is currently just a technical preview,  but the first feature that will be added is configurable snapshot periods.

## Usage

There are two files: `ZomboidPatcher.exe` and `Zomboidhook.dll`

### ZomboidPatcher

This program takes a single argument - namely the path to `ProjectZomboid64.exe` and modifies it to load `ZomboidHook.dll`. You can drag-and-drop the game EXE onto the patcher and it'll do it. **nothing will be displayed** - look at the last-modified time in Windows File Explorer to see if it did it.

Goes without saying; if Project Zomboid gets updated and they change the EXE, you'll need to repatch it. You'll know this is the case because it will either complain your saves are broken or, if there are still old files, then the corresponding state of the save.

### ZomboidHook

This is the hook. Drop it into the game folder after patching and your game is good to go.

## Current Functionality

Right now, only `.bin` files are intercepted so a few other bits of the savegame are left directly on-disk; this is partially because ProjectZomboid itself uses SQLite for a few things (yet, not map chunks, Java API issues perhaps) and data tends to get memmapped which, whilst this could also be faked, would suck out performance and is thus undesirable.

Existing save games are transparently migrated into the database, however, it's incremental insofar as file migration only occurrs when the game requests a particular one. Later I may add behaviour to fully migrate - at the moment though, this is the safest option as it means that you can always "undo" this by simply restoring the original `ProjectZomboid64.exe` file in your game folder.

## Future Functionality

### First

The first planned item will be to implement SQLite snapshots along with code to pull in the few bits of other non-intercepted data that's still sitting on the filesystem. The SQLite used by the game (such as `players.db` will be directly mounted into SQLite and pulled across into the main file)

### Second

A GUI tool to assist with patching and managing the SQLite snapshots for resetting back to whichever snapshot you want.

## For Developers

The application is compiled with Clang 14 on Windows. I have no idea if MSVC can compile it. Code is all C++20 and CMake is used to build it. Pull requests are welcome. I'm very much meaning to add tests and do additional cleanup, if you'd like to do it for me, go for it. Recommended strategy would be to create an executable that the patcher runs on (it'll take any EXE and make it require `ZomboidHook.dll`) and call Windows API functions, validating that the results match what's expected.

Setting up Appveyor for CI would also be a good idea.

### Building

It's a simple CMake project - just download CMake and run the GUI tool if command lines aren't your thing.

