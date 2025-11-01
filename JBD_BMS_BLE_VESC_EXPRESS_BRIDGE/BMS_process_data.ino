bool isPacketValid(byte *packet, int packetLength) {
  if (packetLength < 6) { // Minimum: Start + Cmd + Status + dataLen + 2 Checksum + End
    commSerial.println("❌ Packet too short!");
    return false;
  }

  // Start-Byte prüfen
  if (packet[0] != 0xDD) {
    commSerial.printf("❌ Invalid start byte! Got 0x%02X\n", packet[0]);
    return false;
  }

  // Datenlänge aus Header
  uint8_t dataLen = packet[3];
  int expectedLen = 4 + dataLen + 3; // Start + Cmd + Status + dataLen + 2 Checksum + End

  if (packetLength != expectedLen) {
    commSerial.printf("❌ Packet length mismatch! Got %d, expected %d\n", packetLength, expectedLen);
    return false;
  }

  // End-Byte prüfen
  if (packet[packetLength - 1] != 0x77) {
    commSerial.printf("❌ Invalid end byte 0x%02X (expected 0x77)\n", packet[packetLength - 1]);
    return false;
  }

  // 16-Bit Checksumme berechnen (Status + alle Datenbytes)
  uint16_t calcChecksum = 0;
  // Status = packet[2], Daten = packet[4..4+dataLen-1]
  for (int i = 2; i < 4 + dataLen; i++) {
    calcChecksum += packet[i];
  }
  calcChecksum = (~calcChecksum + 1) & 0xFFFF; // Two's complement 16-bit

  // Empfangene Checksumme aus Paket
  uint8_t rxHigh = packet[4 + dataLen];      // High Byte
  uint8_t rxLow  = packet[4 + dataLen + 1];  // Low Byte
  uint16_t rxChecksum = ((uint16_t)rxHigh << 8) | rxLow;

  commSerial.printf("Checksum (recv=0x%04X calc=0x%04X)\n", rxChecksum, calcChecksum);

  if (rxChecksum != calcChecksum) {
    commSerial.println("❌ Invalid checksum");
    return false;
  }

  return true;
}






bool bmsProcessPacket(byte *packet) {
    TRACE;

    bmsPacketHeaderStruct *pHeader = (bmsPacketHeaderStruct *)packet;
    byte *data = packet + sizeof(bmsPacketHeaderStruct);
    unsigned int dataLen = pHeader->dataLen;

    int packetLength = 4 + dataLen + 2 + 1; // Start+Cmd+Status+dataLen + 2 Checksum + Endbyte

    if (!isPacketValid(packet, packetLength)) {
        commSerial.println("❌ Invalid packet received");
        return false;
    }

    bool result = false;

    switch (pHeader->type) {
        case cBasicInfo3:
            //commSerial.println("BasicInfo3");
            result = processBasicInfo(&packBasicInfo, data, dataLen);
            newPacketReceived = true;
            break;

        case cCellInfo4:
            result = processCellInfo(&packCellInfo, data, dataLen);
            newPacketReceived = true;
            break;

        default:
            commSerial.printf("Unsupported packet type detected. Type: %d\n", pHeader->type);
            result = false;
    }

    return result;
}





bool bleCollectPacket(char *data, uint32_t dataSize) {
    TRACE;

    static uint8_t packetstate = 0;
    static uint8_t packetbuff[50] = {0};
    static uint32_t bufferLen = 0;
    bool retVal = false;

    // Debug Eingang
    //commSerial.printf("DEBUG: Received %lu bytes\n", dataSize);
    //for (uint32_t i = 0; i < dataSize; i++) commSerial.printf("%02X ", (uint8_t)data[i]);
    //commSerial.println("\n------------------------------------------");

    // --- Wenn neues Paket anfängt ---
    if (data[0] == 0xDD) {
        //commSerial.println("bleCollectPacket_1_start");
        packetstate = 1;
        bufferLen = 0;
    }

    // --- Daten IMMER anhängen ---
    for (uint32_t i = 0; i < dataSize; i++) {
        packetbuff[bufferLen + i] = data[i];
    }
    bufferLen += dataSize;

    //commSerial.printf("DEBUG: Appended %lu bytes, buffer length = %lu\n", dataSize, bufferLen);

    // --- Prüfen, ob 0x77 am Ende ---
    if (data[dataSize - 1] == 0x77 && packetstate == 1) {
        //commSerial.println("bleCollectPacket_2_ende");

        // Debug Dump vor Verarbeitung
        commSerial.printf("Total length = %lu bytes\n", bufferLen);
        commSerial.printf("🔍Final buffer dump before validation:\n");
        for (uint32_t i = 0; i < bufferLen; i++) commSerial.printf("%02X ", packetbuff[i]);
        commSerial.printf("\n");
        // Jetzt vollständiges Paket verarbeiten
        bmsProcessPacket(packetbuff);

        // Reset
        packetstate = 0;
        bufferLen = 0;
        memset(packetbuff, 0, sizeof(packetbuff));
        retVal = true;
    }

    return retVal;
}






bool processBasicInfo(packBasicInfoStruct *output, byte *data, unsigned int dataLen) {
  TRACE;
  // Expected data len
  if (dataLen != 0x26) {
  commSerial.printf("fuck - wrong datalen");  
    return false;
  }
  // String myString = String(&data);
  float nominalVoltage = 57.6;
  output->Volts = ((uint32_t)two_ints_into16(data[0], data[1])) * 10;  // Resolution 10 mV -> convert to milivolts   eg 4895 > 48950mV
  output->Amps = ((int32_t)two_ints_into16(data[2], data[3])) * 10;    // Resolution 10 mA -> convert to miliamps

  output->Watts = output->Volts * output->Amps / 1000000;  // W
  output->FullCapacity = ((uint16_t)two_ints_into16(data[6], data[7]));
  output->CapacityRemainAh = ((uint16_t)two_ints_into16(data[4], data[5]));
  output->CapacityRemainPercent = ((uint8_t)data[19]);
  //output->CapacityRemainAh = ((uint16_t)output->FullCapacity * output->CapacityRemainPercent / 100);

  output->Cycles = ((uint16_t)two_ints_into16(data[8], data[9]));


  output->CapacityRemainWh = ((uint32_t)(output->FullCapacity / 100 * (nominalVoltage * 10) * output->CapacityRemainPercent / 100));

  output->Temp1 = (((uint16_t)two_ints_into16(data[23], data[24])) - 2731);
  output->Temp2 = (((uint16_t)two_ints_into16(data[25], data[26])) - 2731);
  output->Temp3 = (((uint16_t)two_ints_into16(data[27], data[28])) - 2731);
  output->BalanceCodeLow = (two_ints_into16(data[12], data[13]));
  output->BalanceCodeHigh = (two_ints_into16(data[14], data[15]));
  output->MosfetStatus = ((byte)data[20]);
  output->BatterySeries = ((uint8_t)data[21]);
  // for (int i = 0; i < dataLen; i++) {
  //   commSerial.print(data[i]);
  // }
  // commSerial.println();

  return true;
};

bool processCellInfo(packCellInfoStruct *output, byte *data, unsigned int dataLen) {

  TRACE;
  uint16_t _cellSum;
  uint16_t _cellMin = 5000;
  uint16_t _cellMax = 0;
  uint16_t _cellAvg;
  uint16_t _cellDiff;

  output->NumOfCells = dataLen / 2;  // Data length * 2 is number of cells !!!!!!

  //go trough individual cells
  for (byte i = 0; i < dataLen / 2; i++) {
    output->CellVolt[i] = ((uint16_t)two_ints_into16(data[i * 2], data[i * 2 + 1]));  // Resolution 1 mV
    _cellSum += output->CellVolt[i];
    if (output->CellVolt[i] > _cellMax) {
      _cellMax = output->CellVolt[i];
    }
    if (output->CellVolt[i] < _cellMin) {
      _cellMin = output->CellVolt[i];
    }

    // output->CellColor[i] = getPixelColorHsv(mapHue(output->CellVolt[i], c_cellAbsMin, c_cellAbsMax), 255, 255);
  }
  output->CellMin = _cellMin;
  output->CellMax = _cellMax;
  output->CellDiff = _cellMax - _cellMin;  // Resolution 10 mV -> convert to volts
  output->CellAvg = _cellSum / output->NumOfCells;

  //----cell median calculation----
  uint16_t n = output->NumOfCells;
  uint16_t i, j;
  uint16_t temp;
  uint16_t x[n];

  for (uint8_t u = 0; u < n; u++) {
    x[u] = output->CellVolt[u];
  }

  for (i = 1; i <= n; ++i)  //sort data
  {
    for (j = i + 1; j <= n; ++j) {
      if (x[i] > x[j]) {
        temp = x[i];
        x[i] = x[j];
        x[j] = temp;
      }
    }
  }

  if (n % 2 == 0)  //compute median
  {
    output->CellMedian = (x[n / 2] + x[n / 2 + 1]) / 2;
  } else {
    output->CellMedian = x[n / 2 + 1];
  }

  for (uint8_t q = 0; q < output->NumOfCells; q++) {
    uint32_t disbal = abs(output->CellMedian - output->CellVolt[q]);
    // output->CellColorDisbalance[q] = getPixelColorHsv(mapHue(disbal, c_cellMaxDisbalance, 0), 255, 255);
  }
  return true;
};



void sendCommand(uint8_t *data, uint32_t dataLen) {
  //TRACE;
  
  NimBLERemoteService* pSvc = nullptr;
  NimBLERemoteCharacteristic* pChr = nullptr;
  NimBLERemoteDescriptor* pDsc = nullptr;

  pChr = pSvc->getCharacteristic(charUUID_tx);
  //bool yepnope = true;
  if (pChr) {
    //commSerial.printf("sizeof(data): %.2f\n", (float)sizeof(data));
    //commSerial.printf("dataLen: %.2f\n", (float)dataLen);
    //pRemoteCharacteristic->writeValue(data, dataLen, false);
    pChr->writeValue(data,dataLen,true);
    #ifdef DEBUG
    commSerial.println("bms request sent");
    #endif
  } else {
    commSerial.println("Remote TX characteristic not found");
  }
}

void bmsGetInfo3() {
  TRACE;
  // header status command length data checksum footer
  //   DD     A5      03     00    FF     FD      77
  uint8_t data[7] = { 0xdd, 0xa5, 0x3, 0x0, 0xff, 0xfd, 0x77 };
  //bmsSerial.write(data, 7);
  sendCommand(data, sizeof(data));
  //commSerial.println("Request info3 sent");
}

void bmsGetInfo4() {
  TRACE;
  //  DD  A5 04 00  FF  FC  77
  uint8_t data[7] = { 0xdd, 0xa5, 0x4, 0x0, 0xff, 0xfc, 0x77 };
  //bmsSerial.write(data, 7);
  sendCommand(data, sizeof(data));
  //commSerial.println("Request info4 sent");
}

void printBasicInfo()  //debug all data to uart
{
  TRACE;
  commSerial.printf("=======================================================================================================================================\n");
  commSerial.printf("Total voltage: %.2f V\n", (float)packBasicInfo.Volts / 1000);
  commSerial.printf("Amps: %.2f A\n", (float)packBasicInfo.Amps / 1000);
  commSerial.printf("Watts: %.2f W\n", (float)packBasicInfo.Watts);
  commSerial.printf("CapacityRemain: %.2f Ah\n", (float)packBasicInfo.CapacityRemainAh / 100);
  commSerial.printf("CapacityRemain: %.2f %%\n", (float)packBasicInfo.CapacityRemainPercent);
  commSerial.printf("CapacityRemain: %.0f Wh\n", (float)packBasicInfo.CapacityRemainWh / 10);
  commSerial.printf("FullCapacity: %.2f Ah\n", (float)packBasicInfo.FullCapacity / 100);
  commSerial.printf("Cycles: %d\n", packBasicInfo.Cycles);
  commSerial.printf("Battery cells in series: %u\n", packBasicInfo.BatterySeries);
  commSerial.printf("Temp1: %.2f°\n", (float)packBasicInfo.Temp1 / 10);
  commSerial.printf("Temp2: %.2f°\n", (float)packBasicInfo.Temp2 / 10);
  commSerial.printf("Temp3: %.2f°\n", (float)packBasicInfo.Temp3 / 10);
  commSerial.printf("Balance Code Low: 0x%x\n", packBasicInfo.BalanceCodeLow);
  commSerial.printf("Balance Code High: 0x%x\n", packBasicInfo.BalanceCodeHigh);
  commSerial.printf("Mosfet Status: 0x%x\n", packBasicInfo.MosfetStatus);
  commSerial.printf("=======================================================================================================================================\n");
}

void printCellInfo()  //debug all data to uart
{
  TRACE;
  commSerial.printf("=======================================================================================================================================\n");
  commSerial.printf("Number of cells: %u\n", packCellInfo.NumOfCells);
  for (byte i = 1; i <= packCellInfo.NumOfCells; i++) {
    commSerial.printf("Cell no. %u", i);
    commSerial.printf("   %.3f\n", (float)packCellInfo.CellVolt[i - 1] / 1000);
  }
  commSerial.printf("Max cell volt: %.3f\n", (float)packCellInfo.CellMax / 1000);
  commSerial.printf("Min cell volt: %.3f\n", (float)packCellInfo.CellMin / 1000);
  commSerial.printf("Difference cell volt: %.3f\n", (float)packCellInfo.CellDiff / 1000);
  commSerial.printf("Average cell volt: %.3f\n", (float)packCellInfo.CellAvg / 1000);
  commSerial.printf("Median cell volt: %.3f\n", (float)packCellInfo.CellMedian / 1000);
  commSerial.printf("=======================================================================================================================================\n");
}

void hexDump(const char *data, uint32_t dataSize)  //debug function
{
  TRACE;
  commSerial.println("HEX data:");

  for (int i = 0; i < dataSize; i++) {
    commSerial.printf("0x%x, ", data[i]);
  }
  commSerial.println("");
}

int16_t two_ints_into16(int highbyte, int lowbyte)  // turns two bytes into a single long integer
{
  TRACE;
  int16_t result = (highbyte);
  result <<= 8;                 //Left shift 8 bits,
  result = (result | lowbyte);  //OR operation, merge the two
  return result;
}

void constructBigString()  //debug all data to uart
{
  TRACE;
  stringBuffer[0] = '\0';  //clear old data
  snprintf(stringBuffer, STRINGBUFFERSIZE, "Total voltage: %f0.00\n", (float)packBasicInfo.Volts / 1000);
  snprintf(stringBuffer, STRINGBUFFERSIZE, "Amps: %f\n", (float)packBasicInfo.Amps / 1000);
  snprintf(stringBuffer, STRINGBUFFERSIZE, "CapacityRemain Ah: %f\n", (float)packBasicInfo.CapacityRemainAh / 1000);
  snprintf(stringBuffer, STRINGBUFFERSIZE, "CapacityRemain %: %d\n", packBasicInfo.CapacityRemainPercent);
  snprintf(stringBuffer, STRINGBUFFERSIZE, "Temp1: %f\n", (float)packBasicInfo.Temp1 / 10);
  snprintf(stringBuffer, STRINGBUFFERSIZE, "Temp2: %f\n", (float)packBasicInfo.Temp2 / 10);
  snprintf(stringBuffer, STRINGBUFFERSIZE, "Temp3: %f\n", (float)packBasicInfo.Temp3 / 10);  
  snprintf(stringBuffer, STRINGBUFFERSIZE, "Balance Code Low: 0x%x\n", packBasicInfo.BalanceCodeLow);
  snprintf(stringBuffer, STRINGBUFFERSIZE, "Balance Code High: 0x%x\n", packBasicInfo.BalanceCodeHigh);
  snprintf(stringBuffer, STRINGBUFFERSIZE, "Mosfet Status: 0x%x\n", packBasicInfo.MosfetStatus);

  snprintf(stringBuffer, STRINGBUFFERSIZE, "Number of cells: %u\n", packCellInfo.NumOfCells);
  for (byte i = 1; i <= packCellInfo.NumOfCells; i++) {
    snprintf(stringBuffer, STRINGBUFFERSIZE, "Cell no. %u", i);
    snprintf(stringBuffer, STRINGBUFFERSIZE, "   %f\n", (float)packCellInfo.CellVolt[i - 1] / 1000);
  }
  snprintf(stringBuffer, STRINGBUFFERSIZE, "Max cell volt: %f\n", (float)packCellInfo.CellMax / 1000);
  snprintf(stringBuffer, STRINGBUFFERSIZE, "Min cell volt: %f\n", (float)packCellInfo.CellMin / 1000);
  snprintf(stringBuffer, STRINGBUFFERSIZE, "Difference cell volt: %f\n", (float)packCellInfo.CellDiff / 1000);
  snprintf(stringBuffer, STRINGBUFFERSIZE, "Average cell volt: %f\n", (float)packCellInfo.CellAvg / 1000);
  snprintf(stringBuffer, STRINGBUFFERSIZE, "Median cell volt: %f\n", (float)packCellInfo.CellMedian / 1000);
  snprintf(stringBuffer, STRINGBUFFERSIZE, "\n");
}
