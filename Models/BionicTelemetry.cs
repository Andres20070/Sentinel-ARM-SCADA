using System.Text.Json.Serialization;

namespace Sentinel.Scada.Web.Models
{

    public class BionicTelemetry
    {
        [JsonPropertyName("pose")]
        public string Pose { get; set; } = "Unknown";

        [JsonPropertyName("angulos")]
        public int[] Angulos { get; set; } = new int[6];

        [JsonPropertyName("sensores")]
        public BionicSensors Sensores { get; set; } = new BionicSensors();
    }

    public class BionicSensors
    {
        [JsonPropertyName("gripper_mA")]
        public float GripperCurrent_mA { get; set; }

        [JsonPropertyName("zone_distance_mm")]
        public int ZoneDistance_mm { get; set; }

        [JsonPropertyName("rgb")]
        public int[] RgbColor { get; set; } = new int[3];
    }
}