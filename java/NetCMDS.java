/**
 * This class sort of emulates a c enum. Call init() once to setup the internal command mappings.
 *
 * @version 0.0.1
 * @author Wonderkat
 */
import java.util.*;

public class NetCMDS {
	private static Map cmdEnum = new HashMap();

	/**
	 * Initializes the internal map with commandnames and values.
	 */
	public static void init() {
		int i = 0;
		cmdEnum.put("UNKNOWN", new Integer(i++));
		cmdEnum.put("MSG", new Integer(i++));
		cmdEnum.put("GETQMSG", new Integer(i++));
		cmdEnum.put("QMSG", new Integer(i++));
		cmdEnum.put("GETLIST", new Integer(i++));
		cmdEnum.put("LIST", new Integer(i++));
		cmdEnum.put("SETPREF", new Integer(i++));
		cmdEnum.put("GETPREF", new Integer(i++));
		cmdEnum.put("PREF", new Integer(i++));
		cmdEnum.put("CHATDOOR", new Integer(i++));
		cmdEnum.put("ACK", new Integer(i++));
		cmdEnum.put("NAK", new Integer(i++));

		cmdEnum.put("C_DOOR", new Integer(i++));
		cmdEnum.put("C_AWAY", new Integer(i++));
		cmdEnum.put("C_BAN", new Integer(i++));
		cmdEnum.put("C_BEEP", new Integer(i++));
		cmdEnum.put("C_BS", new Integer(i++));
		cmdEnum.put("C_CLEAR", new Integer(i++));
		cmdEnum.put("C_DEOP", new Integer(i++));
		cmdEnum.put("C_HELP", new Integer(i++));
		cmdEnum.put("C_KICK", new Integer(i++));
		cmdEnum.put("C_LOG", new Integer(i++));
		cmdEnum.put("C_ME", new Integer(i++));
		cmdEnum.put("C_NICK", new Integer(i++));
		cmdEnum.put("C_OP", new Integer(i++));
		cmdEnum.put("C_OPER", new Integer(i++));
		cmdEnum.put("C_PRIV", new Integer(i++));
		cmdEnum.put("C_PSAY", new Integer(i++));
		cmdEnum.put("C_QUIT", new Integer(i++));
		cmdEnum.put("C_SAY", new Integer(i++));
		cmdEnum.put("C_UNBAN", new Integer(i++));
		cmdEnum.put("C_WHO", new Integer(i++));
	}

	/**
	 * Returns the number of a netcommand.
	 *
	 * @param name	name of the requested command
	 * @return int	number of the named command or -1 if it does not exist
	 */
	public static int getNum(String name) {
		Integer t = (Integer)cmdEnum.get(name);
		return (t != null ? t.intValue() : -1);
	}

	/**
	 * Returns the name of a netcommand.
	 *
	 * @param num		number of the command
	 * @return String	name of the command or null if it does not exist
	 */
	public static String getName(int num) {
		Iterator it = cmdEnum.entrySet().iterator();
		while(it.hasNext()) {
			Map.Entry te = (Map.Entry)it.next();
			if(((Integer)te.getValue()).intValue() == num) return (String)(te.getKey());
		}
		return null;
	}
}

