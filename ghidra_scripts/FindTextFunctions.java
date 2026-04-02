// FindTextFunctions.java - List non-thunk functions in .text with key game names
//@author Claude
//@category Analysis
//@keybinding
//@menupath
//@toolbar

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import java.io.*;

public class FindTextFunctions extends GhidraScript {
    public void run() throws Exception {
        Program p = currentProgram;
        long textStart = 0x00108468L;
        long textEnd   = 0x001b9217L;

        String outPath = "C:/Users/Mariotaku/ghidra_scripts/FindTextFunctions_output.txt";
        PrintWriter out = new PrintWriter(new FileWriter(outPath));

        String[] keywords = {
            "Draw", "Update", "Tick", "Render", "Init", "Create",
            "OnTimer", "OnTouch", "OnKey", "Slice", "Fruit", "Bomb",
            "Wave", "Item", "Score", "Game", "App", "Audio", "Input",
            "FruitNinja", "GlesForm", "MAM", "Mortar"
        };

        out.println("=== NON-THUNK FUNCTIONS IN .text ===");
        out.printf("Range: 0x%x - 0x%x%n", textStart, textEnd);
        out.println();

        int total = 0;
        int interesting = 0;
        StringBuilder interestingFns = new StringBuilder();

        for (Function f : p.getFunctionManager().getFunctions(true)) {
            long addr = f.getEntryPoint().getOffset();
            if (addr < textStart || addr > textEnd) continue;
            if (f.isThunk()) continue;
            total++;

            String name = f.getName(true);
            boolean isInteresting = false;
            for (String kw : keywords) {
                if (name.contains(kw)) { isInteresting = true; break; }
            }
            if (isInteresting) {
                interesting++;
                interestingFns.append(String.format("  0x%08x  %s  [%d params]%n",
                    addr, name, f.getParameterCount()));
            }
        }

        out.println("Total non-thunk in .text: " + total);
        out.println("Interesting (" + interesting + "):");
        out.println(interestingFns.toString());

        out.println("=== ALL NON-THUNK .text FUNCTIONS ===");
        for (Function f : p.getFunctionManager().getFunctions(true)) {
            long addr = f.getEntryPoint().getOffset();
            if (addr < textStart || addr > textEnd) continue;
            if (f.isThunk()) continue;
            out.printf("  0x%08x  %s  [%d params]%n",
                addr, f.getName(true), f.getParameterCount());
        }

        out.close();
        println("Done! Output written to: " + outPath);
    }
}
