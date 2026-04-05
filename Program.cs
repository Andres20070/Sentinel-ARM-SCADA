using Sentinel.Scada.Web.Components;
using Sentinel.Scada.Web.Services;
using System.Net;
using MQTTnet;
using MQTTnet.Server;

var builder = WebApplication.CreateBuilder(args);

// 🚀 1. LEVANTAMOS EL BROKER MQTT (El Cartero) AQUÍ MISMO 🚀
var mqttServerFactory = new MqttServerFactory();
var mqttServerOptions = mqttServerFactory.CreateServerOptionsBuilder()
    .WithDefaultEndpoint()
    .WithDefaultEndpointPort(1883)
    .WithDefaultEndpointBoundIPAddress(IPAddress.Any) // <-- La llave mágica para el ESP32
    .Build();

var mqttServer = mqttServerFactory.CreateMqttServer(mqttServerOptions);
await mqttServer.StartAsync(); // Encendemos el puerto 1883 antes de cargar la web

// 2. Add services to the container.
builder.Services.AddSingleton(mqttServer); // Registramos el broker en memoria
builder.Services.AddSingleton<HistorianService>();
builder.Services.AddSingleton<ScadaMqttService>();
builder.Services.AddRazorComponents()
    .AddInteractiveServerComponents();

var app = builder.Build();

// Configure the HTTP request pipeline.
if (!app.Environment.IsDevelopment())
{
    app.UseExceptionHandler("/Error", createScopeForErrors: true);
    app.UseHsts();
}

app.UseStatusCodePagesWithReExecute("/not-found", createScopeForStatusCodePages: true);
app.UseHttpsRedirection();

// Inicializamos el historiador (SQLite)
app.Services.GetRequiredService<HistorianService>();
app.Services.GetRequiredService<ScadaMqttService>();

app.UseAntiforgery();

app.MapStaticAssets();
app.MapRazorComponents<App>()
    .AddInteractiveServerRenderMode();

app.Run();
