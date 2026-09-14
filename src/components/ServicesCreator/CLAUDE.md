# ServicesCreator

Factory-of-services built on top of `IFactory`.

## Responsibilities

- Creates `IFilesystemService`, `INetworkService`, and `ILLMService` instances
- Passes each service the other services it depends on (e.g. `ILLMService` needs `INetworkService`)
- `Clone()` returns an independent services creator, so a sub-OS can have its own instead of sharing (and outliving) its parent's

## Key Classes

- `ServicesCreator` - Main implementation of `IServicesCreator`
- `CreateServicesCreator(IFactory&)` - Free function to create one
