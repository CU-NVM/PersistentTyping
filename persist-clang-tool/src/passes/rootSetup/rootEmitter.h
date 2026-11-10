#ifndef ROOTEMITTER_H
#define ROOTEMITTER_H

#include "rootTable.h"

// Second half of root-setup, run once after every TU has been collected.
// Writes <main>_pers_root.hpp next to the file with main(): the __pers_root
// struct and the declarations of its accessor and init functions. Then
// rewrites the sources:
//   - every root declaration is removed, or, for a static local that creates
//     its object, replaced by that creation
//   - each global root that creates an object gets __pers_init_<field>()
//     at the end of the file that defines it
//   - main() calls those first, each after every root its initializer uses,
//     otherwise in the order the files were given and then declared
//   - every use becomes the root's slot, as described above
//   - every file it rewrites includes the header
//   - __pers_get_root() is defined once, just before main()
// Files that root-setup has already rewritten (main() already has
// __pers_get_root next to it, and no roots are left) are left alone.
// Returns false, after reporting each problem, without writing anything.
bool emitRoots(const RootTable& table);

#endif
