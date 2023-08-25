function Decoder(bytes, port) {
  // Extract the temperature data from the payload
  var GasSensor = (bytes[1] << 8) | bytes[1];
  
  var GasSensorSimp = GasSensor/512
  // Create the decoded object
  var decodedData = {
    GasSensor: GasSensorSimp
  };

  return decodedData;
}