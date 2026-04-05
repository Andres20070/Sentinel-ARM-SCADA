using MQTTnet;
using MQTTnet.Server;
using Sentinel.Scada.Web.Models;
using System.Text.Json;

namespace Sentinel.Scada.Web.Services
{
    public class ScadaMqttService
    {
        // El evento que avisa a la UI (Blazor)
        public event Action<BionicTelemetry>? OnTelemetryReceived;

        // Inyectamos el servidor que ya se encendió en el Program.cs
        public ScadaMqttService(MqttServer mqttServer)
        {
            // Conectamos nuestro método "ProcessIncomingPayload" al evento del servidor
            mqttServer.InterceptingPublishAsync += ProcessIncomingPayload;
            Console.WriteLine("[SCADA] Servicio MQTT enganchado al Broker embebido exitosamente.");
        }

        private Task ProcessIncomingPayload(InterceptingPublishEventArgs args)
        {
            // Filtramos solo el topic de nuestro brazo robótico
            if (args.ApplicationMessage.Topic == "sentinel/bionic/telemetry")
            {
                try
                {
                    string jsonPayload = args.ApplicationMessage.ConvertPayloadToString();

                    // CHIVATO EN CONSOLA: Para confirmar que llega a C#
                    Console.WriteLine($"[MQTT RCVD] {jsonPayload}");

                    var telemetryData = JsonSerializer.Deserialize<BionicTelemetry>(jsonPayload);

                    if (telemetryData != null)
                    {
                        // Disparamos el evento hacia la UI (Hot Path)
                        OnTelemetryReceived?.Invoke(telemetryData);
                    }
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"[Error] Deserialization failed: {ex.Message}");
                }
            }
            return Task.CompletedTask;
        }
    }
}