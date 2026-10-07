# Submodule source and build policy

Checked-out submodules are read-only source dependencies. Do not edit their
tracked or untracked contents, apply repository patches inside them, or run
generators, bootstrap/configure steps, or builds that write into them. This
applies to temporary changes too: restoring a submodule after a build does not
make an in-place mutation compatible with the policy.

When a submodule is an input to a build, copy or stage the required source into
the corresponding directory under `build/` first. Apply repository-owned
patches to that staged copy and keep compiler products, generated sources,
logs, test outputs, and intermediate files under `build/`. Source-controlled
patch files and scripts belong in the repository, outside the submodule.
