﻿using Fermyon.Spin.Sdk;
using System.Text;

namespace Fermyon.Spin.HelloWorld;

public static class Handler
{
    [HttpHandler(WarmupUrl = "/scroggins")]
    public static HttpResponse HandleHttpRequest(HttpRequest request) => request.Url.ToString() switch
    {
        "/scroggins" => WarmCodePath(request),
        "/redis" => UseRedis(request),
        _ => WarmCodePath(request), //HandleRealRequest(request),
    };

    private static HttpResponse HandleRealRequest(HttpRequest request)
    {
        var onboundRequest = new HttpRequest
        {
            // .WithMethod(Fermyon.Spin.Sdk.HttpMethod.Delete)
            // .WithUri("http://127.0.0.1:3001/hibblebibbdle")
            // .WithBody(System.Text.Encoding.UTF8.GetBytes("see the little goblin, see his little feet"))
            // .WithHeader("X-Outbound-Test", "From .NET")
            // .WithHeader("Accept", "text/plain")
            // .WithQuery("qqq", "qqqqqq");

            Method = Fermyon.Spin.Sdk.HttpMethod.Delete,
            Url = "http://127.0.0.1:3001/hibblebibbdle",
            Headers = new Dictionary<string, string>
            {
                { "X-Outbound-Test", "From .NET" },
                { "Accept", "text/plain" },
            },
            Parameters = new Dictionary<string, string>
            {
                { "myquery", "qqq" },
            },
            Body = Optional.From(Buffer.FromString("see the little goblin, see his little feet")),
        };

        string onboundInfo;

        try
        {
            var response = OutboundHttp.Send(onboundRequest);
            var status = response.Status;
            var onboundSucceeded = status >= 200 && status <= 299;
            var onboundResponseText = status == 200 ?
                response.BodyAsString :
                "<error>";
            onboundInfo = onboundSucceeded ?
                $"The onbound request returned status {status} with {123456 /*TODO: response.Headers.Count()*/} headers ({FormatHeadersShort(response.Headers)}) and the body was:\n{onboundResponseText}\n" :
                $"Tragically the onbound request failed with code {status}\n";
        }
        catch (Exception ex)
        {
            onboundInfo = $"Onbound call exception {ex}";
        }

        var responseText = new StringBuilder();
        responseText.AppendLine($"Called with method {request.Method}, Url {request.Url}");

        foreach (var h in request.Headers)
        {
            responseText.AppendLine($"Header '{h.Key}' had value '{h.Value}'");
        }

        foreach (var p in request.Parameters)
        {
            responseText.AppendLine($"Parameter '{p.Key}' had value '{p.Value}'");
        }

        var bodyInfo = request.Body.TryGetValue(out var bodyBuffer) ?
            $"The body (as a string) was: {Encoding.UTF8.GetString(bodyBuffer.AsSpan())}\n" :
            "The body was empty\n";
        responseText.AppendLine(bodyInfo);

        responseText.AppendLine(onboundInfo);

        return new HttpResponse
        {
            Status = 200,
            Headers = Optional.From(HttpKeyValues.FromDictionary(new Dictionary<string, string>
            {
                { "Content-Type", "text/plain" },
                { "X-TestHeader", "this is a test" },
            })),
            BodyAsString = responseText.ToString(),
        };
    }

    private static HttpResponse WarmCodePath(HttpRequest request)
    {
        // Warmup

        var responseText = new StringBuilder();
        responseText.AppendLine($"Called with method {request.Method}, Url {request.Url}");

        foreach (var h in request.Headers)
        {
            responseText.AppendLine($"Header '{h.Key}' had value '{h.Value}'");
        }

        foreach (var p in request.Parameters)
        {
            responseText.AppendLine($"Parameter '{p.Key}' had value '{p.Value}'");
        }

        var bodyInfo = request.Body.TryGetValue(out var bodyBuffer) ?
            $"The body (as a string) was: {bodyBuffer.ToUTF8String()}\n" :
            "The body was empty\n";
        responseText.AppendLine(bodyInfo);

        return new HttpResponse
        {
            Status = 200,
            Headers = Optional.From(HttpKeyValues.FromDictionary(new Dictionary<string, string>
            {
                { "Content-Type", "text/plain" },
                { "X-TestHeader", "this is a test" },
            })),
            BodyAsString = responseText.ToString(),
        };
    }

    private static HttpResponse UseRedis(HttpRequest request)
    {
        var address = "redis://127.0.0.1:6379";
        var key = "mykey";
        var channel = "messages";

        var payload = request.Body.TryGetValue(out var bodyBuffer) ? bodyBuffer : throw new Exception("cannot read body");
        RedisOutbound.Set(address, key, payload);

        var res = RedisOutbound.Get(address, key).ToUTF8String();

        RedisOutbound.Publish(address, channel, payload);

        return new HttpResponse
        {
            Status = 200,
            BodyAsString = res
        };
    }

    private static string FormatHeadersShort(Optional<HttpKeyValues> optHeaders)
    {
        if (optHeaders.TryGetValue(out var headers))
        {
            return String.Join(" / ", headers.AsSpan().ToArray().Select(kvp => $"{kvp.Key}={kvp.Value}"));
        }
        else
        {
            return "<no headers>";
        }
    }
}
