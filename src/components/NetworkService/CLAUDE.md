# NetworkService

Service-layer wrapper over network access.

## Responsibilities

- Creates `IHTTPClient` instances on demand. This is the only place HTTP
  clients come from: `IFactory` deliberately does not create them, so network
  access stays behind the service layer.

## Key Classes

- `NetworkService` - Main implementation of `INetworkService`
