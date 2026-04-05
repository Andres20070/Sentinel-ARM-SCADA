using System;
using System.Text.Json;
using Microsoft.Data.Sqlite;
using Sentinel.Scada.Web.Models;

namespace Sentinel.Scada.Web.Services
{
    /// <summary>
    /// SCADA Local Data Historian.
    /// Listens to the MQTT stream and persists critical keyframes to SQLite (Cold Path).
    /// </summary>
    public class HistorianService : IDisposable
    {
        private readonly ScadaMqttService _mqttService;
        private readonly string _connectionString = "Data Source=SentinelDB.sqlite;";

        private DateTime _lastSaveTime = DateTime.MinValue;
        private string _lastPose = "";

        public HistorianService(ScadaMqttService mqttService)
        {
            _mqttService = mqttService;
            InitializeDatabase();

            // Subscribirse al evento de telemetría en tiempo real
            _mqttService.OnTelemetryReceived += ProcessTelemetryForStorage;
        }

        private void InitializeDatabase()
        {
            using var connection = new SqliteConnection(_connectionString);
            connection.Open();
            string query = @"
                CREATE TABLE IF NOT EXISTS BionicTelemetry (
                    Id INTEGER PRIMARY KEY AUTOINCREMENT,
                    Timestamp DATETIME NOT NULL,
                    PoseName TEXT,
                    Gripper_mA REAL,
                    Distance_mm INTEGER,
                    PayloadJson TEXT NOT NULL
                );";
            using var command = new SqliteCommand(query, connection);
            command.ExecuteNonQuery();
            Console.WriteLine("[Historian] Base de datos SQLite inicializada.");
        }

        private void ProcessTelemetryForStorage(BionicTelemetry telemetry)
        {
            // Lógica Industrial: Guardar solo cada 2 segundos o si el robot cambió de pose
            if ((DateTime.UtcNow - _lastSaveTime).TotalSeconds > 2 || telemetry.Pose != _lastPose)
            {
                SaveToDatabase(telemetry);
                _lastSaveTime = DateTime.UtcNow;
                _lastPose = telemetry.Pose;
            }
        }

        private void SaveToDatabase(BionicTelemetry telemetry)
        {
            try
            {
                using var connection = new SqliteConnection(_connectionString);
                connection.Open();
                string query = @"
                    INSERT INTO BionicTelemetry (Timestamp, PoseName, Gripper_mA, Distance_mm, PayloadJson) 
                    VALUES (@time, @pose, @current, @distance, @payload)";

                using var command = new SqliteCommand(query, connection);
                command.Parameters.AddWithValue("@time", DateTime.UtcNow.ToString("O")); // Formato ISO 8601
                command.Parameters.AddWithValue("@pose", telemetry.Pose);
                command.Parameters.AddWithValue("@current", telemetry.Sensores.GripperCurrent_mA);
                command.Parameters.AddWithValue("@distance", telemetry.Sensores.ZoneDistance_mm);
                command.Parameters.AddWithValue("@payload", JsonSerializer.Serialize(telemetry));

                command.ExecuteNonQuery();
                Console.WriteLine($"[Historian] Keyframe '{telemetry.Pose}' guardado en disco.");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[Historian Error] Falla al escribir en SQLite: {ex.Message}");
            }
        }

        public void Dispose()
        {
            // Limpieza de memoria vital
            _mqttService.OnTelemetryReceived -= ProcessTelemetryForStorage;
        }

        public async Task<List<BionicTelemetry>> GetRecentHistoryAsync(int limit = 20)
        {
            var results = new List<BionicTelemetry>();
            try
            {
                using var connection = new SqliteConnection(_connectionString);
                await connection.OpenAsync();

                // Traemos los últimos registros ordenados por el más reciente
                string query = "SELECT Timestamp, PayloadJson FROM BionicTelemetry ORDER BY Timestamp DESC LIMIT @limit";
                using var command = new SqliteCommand(query, connection);
                command.Parameters.AddWithValue("@limit", limit);

                using var reader = await command.ExecuteReaderAsync();
                while (await reader.ReadAsync())
                {
                    string json = reader.GetString(1);
                    var telemetry = JsonSerializer.Deserialize<BionicTelemetry>(json);
                    if (telemetry != null)
                    {
                        results.Add(telemetry);
                    }
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[Historian Error] Falla al leer SQLite: {ex.Message}");
            }
            return results;
        }
    }
}