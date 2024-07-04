# Spin SDK for .NET Preview

An experimental SDK for building Spin application components using .NET.

### Features

* Handle HTTP requests using the Spin executor
* Make outbound HTTP requests
* Access Postgres databases
* Make outbound Redis calls
* Fast startup by preparing the .NET runtime during Wasm compilation (via Wizer)

### Prerequisites

You'll need the following to build Spin applications using this SDK:

- [Spin](https://spin.fermyon.dev) v0.5.0 or above
- [.NET 8+](https://dotnet.microsoft.com/en-us/download/dotnet/8.0)
- [Wizer](https://github.com/bytecodealliance/wizer/releases) - download and place it on your PATH
  - If you have Rust installed, you can install Wizer by running `make bootstrap` in the root of the SDK repo

### Getting the Spin SDK

You can get the SDK itself from NuGet via `dotnet add package Fermyon.Spin.Sdk --prerelease`, or use
the provided template - see below for details.

### Building the "hello world" sample

To build and run the `hello-world` sample, clone this repo and run:

```
$ cd samples/hello-world
$ spin build --up
```

If everything worked, you should see a Spin "serving routes" message:

```
Serving http://127.0.0.1:3000
Available Routes:
  hello: http://127.0.0.1:3000 (wildcard)
```

You should be able to curl the address and get a response along the lines of:

```
$ curl -v 127.0.0.1:3000
// outbound trace omitted
< HTTP/1.1 200 OK
< content-type: text/plain
< x-testheader: this is a test
< content-length: 451
< date: Thu, 21 Jul 2022 03:11:15 GMT
<
Called with method Get on URL /
Header 'host' had value '127.0.0.1:3000'
// ... more headers info ...
Header 'spin-component-route' had value ''
The body was empty
```

### Installing the Spin application template

The SDK includes a Spin template for C# projects.  To install it, run:

```
spin templates install --git https://github.com/fermyon/spin-dotnet-sdk --branch main --update
```

You can then run `spin new -t http-csharp <project-name>` to create a new Spin C# application.

> If you're creating a project without using the Spin template, add a reference the Spin SDK with the command
> `dotnet add package Fermyon.Spin.Sdk --prerelease`

### Handling HTTP requests

Your .NET project should contain a method with the `Fermyon.Spin.Sdk.HttpHandler` attribute.
This method must be `static`, and must take one argument of type `Fermyon.Spin.Sdk.HttpRequest`
and return a `Fermyon.Spin.Sdk.HttpResponse`.

```csharp
using Fermyon.Spin.Sdk;

public static class MyHandler
{
    [HttpHandler]
    public static HttpResponse HandleHttpRequest(HttpRequest request)
    {
        // ...
    }
}
```

Your `spin.toml` file should reference the compiled Wasm file built from the project.

```toml
[component.test]
source = "bin/Release/net8.0/MyApplication.wasm"
```

### Making outbound HTTP requests

To make outbound HTTP requests, use the `HttpOutbound.Send()` method.

For an example of constructing an outbound request, see the `UseOutboundHttp` method
in the `hello-world` sample.

### Making Redis requests

To make outbound Redis requests, use the methods of the `RedisOutbound` class -
`Get`, `Set` and `Publish`.

For examples of making Redis requests, see the `UseRedis` method
in the `hello-world` sample.

### Working with Postgres

To access Postgres databases, use the methods of the `PostgresOutbound` class -
`Query` for statements that return database values (`SELECT`), and `Execute`
for statements that modify the database (`INSERT`, `UPDATE`, `DELETE`).

For examples of making Postgres requests, see the `UsePostgresQuery` and
`UsePostgresExec` methods in the `hello-world` sample, or see the
`Fermyon.PetStore` sample.

### Accessing Spin configuration

To access Spin configuration, use the `SpinConfig.Get()` method.

> It is not expected that a Spin component will try to access config entries
> that don't exist. At the moment, the only way to detect if a config setting
> is missing is to catch the exception from `Get`.

For examples of accessing configuration, see the samples.

### Working with Buffers

Both HTTP and Redis represent payload blobs using the `Buffer` type, and Postgres also
uses `Buffer` for values of `binary` (aka 'blob') type. Buffer represents
an unmanaged span of Wasm linear memory.  The SDK provides several convenience methods
to make it easier to work with.  For example:

* Use `HttpRequest.Body.AsString()` and `HttpRequest.Body.AsBytes()` to read a request
  body as text or a binary blob.
* Use `Buffer.ToUTF8String()` to read an arbitrary Buffer as string.
* Use `HttpResponse.BodyAsString` and `HttpResponse.BodyAsBytes` to set the body of
  a HTTP response.
* Use `Buffer.FromString()` to write a string into a buffer using the UTF-8 encoding.
* Use `Buffer.FromBytes()` to write any sequence of bytes into a buffer.

### Fast startup using Wizer

If your project file (`.csproj`) contains `<UseWizer>true</UseWizer>`, then `dotnet build`
will run [Wizer](https://github.com/bytecodealliance/wizer) to pre-initialise your
Wasm file.  Wizer will run a request through your application _at compile time_ and snapshot
the state of your Wasm module at the end of the request.  This means that the resulting
Wasm module contains the .NET runtime in a state where it is already loaded (and the
interpreter has already seen your code), saving startup time when a request comes in at runtime.

> You must run install [Wizer](https://github.com/bytecodealliance/wizer/releases) and place
> it on your path (or run `make bootstrap`).

Using Wizer has certain observable impacts:

* You should not (and in some cases cannot) call external services from the warmup request
  handler.  If your handler talks to HTTP or Redis, you _must skip those calls at warmup time_.
  If the warmup code fails, then the build will fail.
* Static constructors and static member initialisation happens at warmup time (at least for
  any type used on the warmup path).  For example, if your handler type has a static
  `Random` member, and this gets initialised during warmup, then _the same state of the random
  number generator_ will be used in all requests!

You can identify if a request is the warmup request because the URL will be `/warmupz`.
You can override this in the `HttpHandler` attribute.  However, it is not currently possible
to have Wizer initialise the runtime but omit calling your handler.

If your handler logic doesn't require any external services then you don't need any special
warmup handling.  Otherwise, you'll need to guard those calls, or skip your real handler
altogether, e.g.:

```csharp
[HttpHandler]
public static HttpResponse HandleHttpRequest(HttpRequest request)
{
    if (request.Url == Warmup.DefaultWarmupUrl)
    {
        return new HttpResponse
        {
            StatusCode = HttpStatusCode.OK,
            Headers = new Dictionary<string, string>
            {
                { "Content-Type", "text/plain" },
            },
            BodyAsString = "warmup",
        };
    }

    // ... real handler goes here ...
}
```

## Known issues

The Spin .NET SDK is a preview, built on an implementation of .NET that is currently experimental.
There are several known issues, of which the most severe are:

* Some static methods and properties cause a "indirect call type mismatch" error when Wizer is turned
  on - we have seen this on numeric parse methods and `StringComparer` properties.
  You can work around this by turning Wizer off for affected modules. To do this, change
  `<UseWizer>true</UseWizer>` in the `.csproj` to `<UseWizer>false</UseWizer>`.
* In some cases, unhandled exceptions also cause "indirect call type mismatch" instead of being
  returned as 500 Internal Server Error responses. You can work around this by catching problematic
  exceptions and returning error responses manually.

You can track issues or report problems at https://github.com/fermyon/spin-dotnet-sdk/issues.

## What's next

The initial version of the SDK closely mirrors the underlying low-level Spin interop interfaces.
This maximises performance but doesn't provide an idiomatic experience for .NET developers.
We'll be aiming to improve that over future releases, and welcome contributions or suggestions!
