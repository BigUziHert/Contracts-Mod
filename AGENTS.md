# Development workflow

- Work on the existing `dev` branch by default, as requested by the owner.
- Publish changes to `main` when the owner requests it; finish with `dev` checked out.
- Build downloadable ASI files into `dist` by overriding the project's `OutDir`.
  The project default points at a local game installation, so do not deploy there
  unless deployment is requested.
