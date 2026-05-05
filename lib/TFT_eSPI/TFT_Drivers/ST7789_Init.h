
// This is the command sequence that initialises the ST7789 driver
//
// This setup information uses simple 8 bit SPI writecommand() and writedata() functions
//
// See ST7735_Setup.h file for an alternative format

{
  writecommand(ST7789_SLPOUT);   // Sleep out
  delay(120);

  writecommand(ST7789_NORON);    // Normal display mode on

  //------------------------------display and color format setting--------------------------------//
  writecommand(ST7789_MADCTL);
  //writedata(0x00);
  writedata(TFT_MAD_COLOR_ORDER);

  // JLX240 display datasheet
  // writecommand(0xB6);
  // writedata(0x0A);
  // writedata(0x82);

  // writecommand(ST7789_RAMCTRL);
  // writedata(0x00);
  // writedata(0xE0); // 5 to 6 bit conversion: r0 = r5, b0 = b5

  writecommand(ST7789_COLMOD);
  writedata(0x55);
  delay(10);

  //--------------------------------ST7789V Frame rate setting----------------------------------//
  writecommand(ST7789_PORCTRL);
  writedata(0x0B);
  writedata(0x0B);
  writedata(0x00);
  writedata(0x33);
  writedata(0x33);

  writecommand(ST7789_GCTRL);      // Voltages: VGH / VGL
  writedata(0x11);

  //---------------------------------ST7789V Power setting--------------------------------------//
  writecommand(ST7789_VCOMS);
  writedata(0x2F);		// JLX240 display datasheet

  writecommand(ST7789_LCMCTRL);
  writedata(0x2C);

  writecommand(ST7789_VDVVRHEN);
  writedata(0x01);
  // writedata(0xFF);

  writecommand(ST7789_VRHS);       // voltage VRHS
  writedata(0x0D);

  writecommand(ST7789_VDVSET);
  writedata(0x20);

  writecommand(ST7789_FRCTR2);
  writedata(0x18);

  writecommand(ST7789_PWCTRL1);
  writedata(0xa4);
  writedata(0xa1);

  //--------------------------------ST7789V gamma setting---------------------------------------//
  writecommand(ST7789_PVGAMCTRL);
  writedata(0xF0);
  writedata(0x06);
  writedata(0x0B);
  writedata(0x0A);
  writedata(0x09);
  writedata(0x26);
  writedata(0x29);
  writedata(0x33);
  writedata(0x41);
  writedata(0x18);
  writedata(0x16);
  writedata(0x15);
  writedata(0x29);
  writedata(0x2D);

  writecommand(ST7789_NVGAMCTRL);
  writedata(0xF0);
  writedata(0x04);
  writedata(0x08);
  writedata(0x08);
  writedata(0x07);
  writedata(0x03);
  writedata(0x28);
  writedata(0x32);
  writedata(0x40);
  writedata(0x3B);
  writedata(0x19);
  writedata(0x18);
  writedata(0x2A);
  writedata(0x2E);

  writecommand(0xE4);
  writedata(0x25);
  writedata(0x00);
  writedata(0x00);

  writecommand(ST7789_INVON);

  writecommand(ST7789_CASET);    // Column address set
  writedata(0x00);
  writedata(0x00);
  writedata(0x00);
  writedata(0xE5);    // 239

  writecommand(ST7789_RASET);    // Row address set
  writedata(0x00);
  writedata(0x00);
  writedata(0x01);
  writedata(0x3F);    // 319

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  end_tft_write();
  delay(120);
  begin_tft_write();

  writecommand(ST7789_DISPON);    //Display on
  delay(120);

#ifdef TFT_BL
  // Turn on the back-light LED
  digitalWrite(TFT_BL, HIGH);
  pinMode(TFT_BL, OUTPUT);
#endif
}
