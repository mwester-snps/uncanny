// Implementation of a nonsense security algorithm for CAN

import com.codenomicon.api.custom.ProprietaryCanAlgorithm;

import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

public class Uncanny implements ProprietaryCanAlgorithm {

  @Override
  public byte[] calculateChecksum(byte[] identifier, byte[] dlc, byte[] data) {
    MessageDigest md;
    try {
      md = MessageDigest.getInstance("md5");
    } catch (NoSuchAlgorithmException e) {
      return null;
    }
    md.update(identifier);
    md.update(dlc);
    md.update(data);
    byte[] result = md.digest();
    return new byte[] {result[0]};
  }

  @Override
  public byte[] calculateSecurityAccessKey(byte[] identifier, byte[] seed) {
    // sample implmentation hard-codes key, ignores seed...  Horrors!
    byte[] key = new byte[2];
    key[0] = 0x32;
    key[1] = 0x10;
    return key;
  }
}
