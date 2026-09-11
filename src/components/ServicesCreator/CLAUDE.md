# ServicesCreator

Factory-of-services built on top of `IFactory`.

## Responsibilities

- Creates `IFilesystemService`, `INetworkService`, and `ILLMService` instances
- Passes each service the other services it depends on (e.g. `ILLMService` needs `INetworkService`)

## Key Classes

- `ServicesCreator` - Main implementation of `IServicesCreator`
- `CreateServicesCreator(IFactory&)` - Free function to create one
