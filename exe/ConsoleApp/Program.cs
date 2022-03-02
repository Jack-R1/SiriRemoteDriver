/*
 * Based on code from 
 * https://github.com/microsoft/Windows-universal-samples/tree/main/Samples/BluetoothLE
 * and
 * https://github.com/CarterAppleton/Win10Win32Bluetooth
 */
using System;
using System.Text;
using System.Threading.Tasks;

using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Storage.Streams;

namespace ConsoleApp
{
    class Program
    {
        static string deviceName = "";
        static string deviceMAC = "";

        static string deviceAddress;
        static ulong deviceAddressNumber;

        static void Usage()
        {
            log("Usage:");
            log("ConsoleApp <DEVICENAME> <DEVICEMAC>");
            log("E.g. ConsoleApp DJ7XXXXXXXXM FF:FF:FF:FF:FF:FF");
        }

        static void Main(string[] args)
        {
            if (args.Length < 2)
            {
                Usage();
                return;
            }

            deviceName = args[0].Replace("\"", "");
            deviceMAC = args[1].Replace("\"", "");

            deviceAddress = deviceMAC.Replace(":", "").ToUpper();
            deviceAddressNumber = ulong.Parse(deviceMAC.Replace(":", "").ToUpper(), System.Globalization.NumberStyles.HexNumber);

            //https://stackoverflow.com/questions/9208921/cant-specify-the-async-modifier-on-the-main-method-of-a-console-app
            Task.Run(async () =>
            {
                // Do anything async here without worry

                await SendMagicPacket();

            }).GetAwaiter().GetResult();

            // Keep console application open to receive notifications
            Console.ReadLine();
        }

        enum AttributeType
        {
            Service = 0,
            Characteristic = 1,
            Descriptor = 2
        }

        /// <summary>
        ///     The SIG has a standard base value for Assigned UUIDs. In order to determine if a UUID is SIG defined,
        ///     zero out the unique section and compare the base sections.
        /// </summary>
        /// <param name="uuid">The UUID to determine if SIG assigned</param>
        /// <returns></returns>
        static bool IsSigDefinedUuid(Guid uuid)
        {
            var bluetoothBaseUuid = new Guid("00000000-0000-1000-8000-00805F9B34FB");

            var bytes = uuid.ToByteArray();
            // Zero out the first and second bytes
            // Note how each byte gets flipped in a section - 1234 becomes 34 12
            // Example Guid: 35918bc9-1234-40ea-9779-889d79b753f0
            //                   ^^^^
            // bytes output = C9 8B 91 35 34 12 EA 40 97 79 88 9D 79 B7 53 F0
            //                ^^ ^^
            bytes[0] = 0;
            bytes[1] = 0;
            var baseUuid = new Guid(bytes);
            return baseUuid == bluetoothBaseUuid;
        }

        /// <summary>
        ///     Converts from standard 128bit UUID to the assigned 32bit UUIDs. Makes it easy to compare services
        ///     that devices expose to the standard list.
        /// </summary>
        /// <param name="uuid">UUID to convert to 32 bit</param>
        /// <returns></returns>
        static ushort ConvertUuidToShortId(Guid uuid)
        {
            // Get the short Uuid
            var bytes = uuid.ToByteArray();
            var shortUuid = (ushort)(bytes[0] | (bytes[1] << 8));
            return shortUuid;
        }

        static string GetName(AttributeType AttributeDisplayType, object GattObject)
        {
            GattDeviceService service = null;
            GattCharacteristic characteristic = null;

            switch (AttributeDisplayType)
            {
                case AttributeType.Service:
                    service = (GattDeviceService)GattObject;
                    if (IsSigDefinedUuid(service.Uuid))
                    {
                        GattNativeServiceUuid serviceName;
                        if (Enum.TryParse(ConvertUuidToShortId(service.Uuid).ToString(), out serviceName))
                        {
                            return serviceName.ToString();
                        }
                    }
                    else
                    {
                        return "Custom Service: " + service.Uuid;
                    }
                    break;
                case AttributeType.Characteristic:
                    characteristic = (GattCharacteristic)GattObject;
                    if (IsSigDefinedUuid(characteristic.Uuid))
                    {
                        GattNativeCharacteristicUuid characteristicName;
                        if (Enum.TryParse(ConvertUuidToShortId(characteristic.Uuid).ToString(),
                            out characteristicName))
                        {
                            if (!string.IsNullOrEmpty(characteristic.UserDescription))
                            {
                                return $"{characteristicName.ToString()} ({characteristic.UserDescription})";
                            }
                            else
                            {
                                return characteristicName.ToString();
                            }
                        }
                    }
                    else
                    {
                        if (!string.IsNullOrEmpty(characteristic.UserDescription))
                        {
                            return characteristic.UserDescription;
                        }

                        else
                        {
                            return "Custom Characteristic: " + characteristic.Uuid;
                        }
                    }
                    break;
                default:
                    break;
            }
            return "Invalid";
        }

        static void log(string v)
        {
            Console.WriteLine(v);
        }

        /// <summary>
        /// Send SiriRemote magic packet on to the battery service UUID
        /// Our filter driver installed on the system will intercept the
        /// calls and replace the channel id to the the restricted hid service
        /// </summary>
        static async Task SendMagicPacket()
        {
            //This works only if your device is already paired!

            //we get the bluetooth address number by debug the connect function
            BluetoothLEDevice bleDevice = await BluetoothLEDevice.FromBluetoothAddressAsync(deviceAddressNumber);

            if (bleDevice != null && (bleDevice.Name.Equals(deviceName) || bleDevice.Name.Contains(deviceMAC)))
            {
                //Battery Service
                GattDeviceServicesResult servicesResult = await bleDevice.GetGattServicesForUuidAsync(new Guid("0000180F-0000-1000-8000-00805f9b34fb"), BluetoothCacheMode.Uncached);
                GattDeviceService batteryservice = servicesResult.Services[0];

                GattCharacteristicsResult characteristicsResult = await batteryservice.GetCharacteristicsAsync(BluetoothCacheMode.Uncached);

                foreach (GattCharacteristic characteristic in characteristicsResult.Characteristics)
                {
                    //UUID: 00002a1a-0000-1000-8000-00805f9b34fb, Name: BatteryPowerState
                    if (characteristic.Uuid == new Guid("00002a1a-0000-1000-8000-00805f9b34fb"))
                    {
                        log($"attr handle: {characteristic.AttributeHandle.ToString("X")}, Characteristic UUID: {characteristic.Uuid.ToString()}, Name: {GetName(AttributeType.Characteristic, characteristic)}");
                        log("Registering for BatteryPowerState notifications where HID notifications will be redirected to by SiriRemoteFilterDriver.");

                        if (characteristic.CharacteristicProperties.HasFlag(GattCharacteristicProperties.Notify))
                        {
                            try
                            {
                                // Write the ClientCharacteristicConfigurationDescriptor in order for server to send notifications.               
                                var result = await characteristic.WriteClientCharacteristicConfigurationDescriptorAsync(
                                                                          GattClientCharacteristicConfigurationDescriptorValue.Notify);
                                if (result == GattCommunicationStatus.Success)
                                {
                                    log("Successfully registered for notifications");
                                    characteristic.ValueChanged += SelectedCharacteristic_ValueChanged;
                                }
                                else
                                {
                                    log($"Error registering for notifications: {result}");
                                }
                            }
                            catch (Exception ex)
                            {
                                log($"Notify Exception: {ex.Message}");
                            }
                        }
                    }
                }

                log("");

                foreach (GattCharacteristic characteristic in characteristicsResult.Characteristics)
                {
                    //UUID: 00002a19-0000-1000-8000-00805f9b34fb, Name: BatteryLevel
                    if (characteristic.Uuid == new Guid("00002a19-0000-1000-8000-00805f9b34fb"))
                    {
                        log($"attr handle: {characteristic.AttributeHandle.ToString("X")}, Characteristic UUID: {characteristic.Uuid.ToString()}, Name: {GetName(AttributeType.Characteristic, characteristic)}");
                        log("Sending SiriRemote magic packets via BatteryLevel so SiriRemoteFilterDriver can trigger and intercept HID notifications.");

                        byte[] bytesToSend;
                        DataWriter writer;
                        IBuffer dataToSend;

                        GattDescriptorsResult descriptorsResult = await characteristic.GetDescriptorsAsync(BluetoothCacheMode.Uncached);

                        foreach (GattDescriptor batdesc in descriptorsResult.Descriptors)
                        {
                            bytesToSend = new byte[] { 0x01, 0x00 };

                            writer = new DataWriter();

                            writer.WriteBytes(bytesToSend);

                            dataToSend = writer.DetachBuffer();

                            Task.Run(async () =>
                            {
                                // Do any async anything you need here without worry
                                await batdesc.WriteValueAsync(dataToSend);

                            }).GetAwaiter().GetResult();

                            log("Writing 0x01, 0x00");
                        }

                        bytesToSend = new byte[] { 0xAF };

                        writer = new DataWriter();

                        writer.WriteBytes(bytesToSend);

                        dataToSend = writer.DetachBuffer();

                        Task.Run(async () =>
                        {
                            // Do any async anything you need here without worry
                            await characteristic.WriteValueAsync(dataToSend, GattWriteOption.WriteWithoutResponse);

                        }).GetAwaiter().GetResult();

                        log("Writing 0xAF");
                    }
                }

                log("");

                //for (int i = 0; i < 3; i++)
                //    await Task.Delay(1000);
            }
        }

        static void SelectedCharacteristic_ValueChanged(GattCharacteristic sender, GattValueChangedEventArgs args)
        {
            var RawBufLen = (int)args.CharacteristicValue.Length;
            byte[] data;
            string strData = null;


            Windows.Security.Cryptography.CryptographicBuffer.CopyToByteArray(args.CharacteristicValue, out data);
            //byte[] bytes = WindowsRuntimeBufferExtensions.ToArray(result.Value, 0, (int)result.Value.Length);

            StringBuilder hex = new StringBuilder(data.Length * 2);

            for (int j = 0; j < data.Length; j++)
            {
                char ch = Convert.ToChar(data[j]);
                hex.AppendFormat(" {0:x2}", data[j]);

                if (ch != '\0')
                {
                    strData = strData + ch;
                }
            }

            //log($"Value Changed : {strData} ({hex})");

            //voice data should be around 102 bytes
            if (data.Length > 13 && data.Length < 100)
                hex.AppendFormat("...<- Voice data most likely truncated.");

            log($"Notification :{hex}");
        }

        /// <summary>
        ///     This enum assists in finding a string representation of a BT SIG assigned value for Service UUIDS
        ///     Reference: https://developer.bluetooth.org/gatt/services/Pages/ServicesHome.aspx
        /// </summary>
        enum GattNativeServiceUuid : ushort
        {
            None = 0,
            AlertNotification = 0x1811,
            Battery = 0x180F,
            BloodPressure = 0x1810,
            BondManagementService = 0x181E,
            CurrentTimeService = 0x1805,
            CyclingSpeedandCadence = 0x1816,
            DeviceInformation = 0x180A,
            GenericAccess = 0x1800,
            GenericAttribute = 0x1801,
            Glucose = 0x1808,
            HealthThermometer = 0x1809,
            HeartRate = 0x180D,
            HumanInterfaceDevice = 0x1812,
            ImmediateAlert = 0x1802,
            LinkLoss = 0x1803,
            NextDSTChange = 0x1807,
            PhoneAlertStatus = 0x180E,
            ReferenceTimeUpdateService = 0x1806,
            RunningSpeedandCadence = 0x1814,
            ScanParameters = 0x1813,
            TxPower = 0x1804,
            SimpleKeyService = 0xFFE0
        }

        /// <summary>
        ///     This enum is nice for finding a string representation of a BT SIG assigned value for Characteristic UUIDs
        ///     Reference: https://developer.bluetooth.org/gatt/characteristics/Pages/CharacteristicsHome.aspx
        /// </summary>
        enum GattNativeCharacteristicUuid : ushort
        {
            None = 0,
            AlertCategoryID = 0x2A43,
            AlertCategoryIDBitMask = 0x2A42,
            AlertLevel = 0x2A06,
            AlertNotificationControlPoint = 0x2A44,
            AlertStatus = 0x2A3F,
            Appearance = 0x2A01,
            BatteryLevel = 0x2A19,
            BloodPressureFeature = 0x2A49,
            BloodPressureMeasurement = 0x2A35,
            BodySensorLocation = 0x2A38,
            BootKeyboardInputReport = 0x2A22,
            BootKeyboardOutputReport = 0x2A32,
            BootMouseInputReport = 0x2A33,
            BondManagementControlPoint = 0x2AA4,
            BondManagementFeatures = 0x2AA5,
            CSCFeature = 0x2A5C,
            CSCMeasurement = 0x2A5B,
            CurrentTime = 0x2A2B,
            DateTime = 0x2A08,
            DayDateTime = 0x2A0A,
            DayofWeek = 0x2A09,
            DeviceName = 0x2A00,
            DSTOffset = 0x2A0D,
            ExactTime256 = 0x2A0C,
            FirmwareRevisionString = 0x2A26,
            GlucoseFeature = 0x2A51,
            GlucoseMeasurement = 0x2A18,
            GlucoseMeasurementContext = 0x2A34,
            HardwareRevisionString = 0x2A27,
            HeartRateControlPoint = 0x2A39,
            HeartRateMeasurement = 0x2A37,
            HIDControlPoint = 0x2A4C,
            HIDInformation = 0x2A4A,
            IEEE11073_20601RegulatoryCertificationDataList = 0x2A2A,
            IntermediateCuffPressure = 0x2A36,
            IntermediateTemperature = 0x2A1E,
            LocalTimeInformation = 0x2A0F,
            ManufacturerNameString = 0x2A29,
            MeasurementInterval = 0x2A21,
            ModelNumberString = 0x2A24,
            NewAlert = 0x2A46,
            PeripheralPreferredConnectionParameters = 0x2A04,
            PeripheralPrivacyFlag = 0x2A02,
            PnPID = 0x2A50,
            ProtocolMode = 0x2A4E,
            ReconnectionAddress = 0x2A03,
            RecordAccessControlPoint = 0x2A52,
            ReferenceTimeInformation = 0x2A14,
            Report = 0x2A4D,
            ReportMap = 0x2A4B,
            RingerControlPoint = 0x2A40,
            RingerSetting = 0x2A41,
            RSCFeature = 0x2A54,
            RSCMeasurement = 0x2A53,
            SCControlPoint = 0x2A55,
            ScanIntervalWindow = 0x2A4F,
            ScanRefresh = 0x2A31,
            SensorLocation = 0x2A5D,
            SerialNumberString = 0x2A25,
            ServiceChanged = 0x2A05,
            SoftwareRevisionString = 0x2A28,
            SupportedNewAlertCategory = 0x2A47,
            SupportedUnreadAlertCategory = 0x2A48,
            SystemID = 0x2A23,
            TemperatureMeasurement = 0x2A1C,
            TemperatureType = 0x2A1D,
            TimeAccuracy = 0x2A12,
            TimeSource = 0x2A13,
            TimeUpdateControlPoint = 0x2A16,
            TimeUpdateState = 0x2A17,
            TimewithDST = 0x2A11,
            TimeZone = 0x2A0E,
            TxPowerLevel = 0x2A07,
            UnreadAlertStatus = 0x2A45,
            AggregateInput = 0x2A5A,
            AnalogInput = 0x2A58,
            AnalogOutput = 0x2A59,
            CyclingPowerControlPoint = 0x2A66,
            CyclingPowerFeature = 0x2A65,
            CyclingPowerMeasurement = 0x2A63,
            CyclingPowerVector = 0x2A64,
            DigitalInput = 0x2A56,
            DigitalOutput = 0x2A57,
            ExactTime100 = 0x2A0B,
            LNControlPoint = 0x2A6B,
            LNFeature = 0x2A6A,
            LocationandSpeed = 0x2A67,
            Navigation = 0x2A68,
            NetworkAvailability = 0x2A3E,
            PositionQuality = 0x2A69,
            ScientificTemperatureinCelsius = 0x2A3C,
            SecondaryTimeZone = 0x2A10,
            String = 0x2A3D,
            TemperatureinCelsius = 0x2A1F,
            TemperatureinFahrenheit = 0x2A20,
            TimeBroadcast = 0x2A15,
            BatteryLevelState = 0x2A1B,
            BatteryPowerState = 0x2A1A,
            PulseOximetryContinuousMeasurement = 0x2A5F,
            PulseOximetryControlPoint = 0x2A62,
            PulseOximetryFeatures = 0x2A61,
            PulseOximetryPulsatileEvent = 0x2A60,
            SimpleKeyState = 0xFFE1
        }

        /// <summary>
        ///     This enum assists in finding a string representation of a BT SIG assigned value for Descriptor UUIDs
        ///     Reference: https://developer.bluetooth.org/gatt/descriptors/Pages/DescriptorsHomePage.aspx
        /// </summary>
        enum GattNativeDescriptorUuid : ushort
        {
            CharacteristicExtendedProperties = 0x2900,
            CharacteristicUserDescription = 0x2901,
            ClientCharacteristicConfiguration = 0x2902,
            ServerCharacteristicConfiguration = 0x2903,
            CharacteristicPresentationFormat = 0x2904,
            CharacteristicAggregateFormat = 0x2905,
            ValidRange = 0x2906,
            ExternalReportReference = 0x2907,
            ReportReference = 0x2908
        }
    }
}
