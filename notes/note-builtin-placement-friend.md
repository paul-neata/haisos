# Placing builtins: protected on IFileSystem, with a friend -- and fix the IAgent friend

**To do: make `IFileSystem::AddBuiltinCommand` and `RemoveBuiltinCommand`
protected.** They are public today (`interfaces/IFileSystemService.h`), though
their own comment says placing a builtin is how an OS is assembled, and that
`IBuiltinConfigurator` is the front door, adding the checks a placement needs:
the directory must exist, and nothing may be at the path. Anyone holding an
`IFileSystem` can go around those checks. Make them protected, with the
configurator as `friend` (currently `BuiltinConfigurator`, in the
BuiltinCommands component -- the only caller in `src/`), the way
`IAgent::AddChild` is done. To handle:

- no override may widen them again: `MountableFileSystem` declares them public
  and `final` (they would become protected), and so does the test double in
  `tests/mocks/MockFilesystem.h`;
- the filesystem tests that call them directly
  (`tests/unit/components/Filesystem.unittests/`, the builtin-file and device
  tests) check the filesystem's own rules -- no builtin at the root, none twice,
  none on a device filesystem -- which the configurator's checks would mostly
  reach first. They need a way in of their own.

**Fix the other friend: `IAgent::AddChild`.** `interfaces/ILLMService.h` makes
it protected, with `friend class LLMService`, so that only the service creating
agents registers children. But the overrides widen it back to public:

- the concrete `Agent` does, with a comment saying nothing outside the Agent
  component and its tests can reach `Agent` -- yet `HaisosOS` casts to it, and
  `AgentProcess` holds one;
- so does `MockAgent`, on which the tool tests call it directly.

So the protection holds only for callers holding a bare `IAgent`. Keep it
protected in every class, and give the tests a sanctioned way in.

Both friends name a concrete class in `interfaces/`, which otherwise knows no
implementation. Decide one pattern for the two, and apply it to both:

- a plain `friend` of the implementing class;
- a passkey type that only that class can construct -- the method then stays
  public, so overrides and mocks need no widening;
- moving the method to a narrower interface that only the creator holds.
