import org.json.simple.*;

public class AIClient {
	public static void main(String[] argv) {		
		JSONObject obj = (JSONObject)JSONValue.parse(argv[0]);
		Board board = Board.initializeBoardFromJSON(obj);
		
		// the following "AI" moves a piece as far left as possible
		while (board._block.checkedLeft(board)) {
			System.out.println("left");
		}
		System.out.flush();
	}
}