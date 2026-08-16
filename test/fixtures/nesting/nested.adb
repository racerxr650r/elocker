--  nested.adb — statements and decision points attributed to the innermost
--  enclosing subprogram. Hand-counted in README.md beside this file.

procedure Outer (Seed : Integer) is
   Total : Integer := Seed;

   function Middle (A : Integer) return Integer is
      function Inner (B : Integer) return Integer is
      begin
         if B > 0 then
            return B * 2;
         end if;
         return 0;
      end Inner;
   begin
      if A > 0 then
         return Inner (A) + 1;
      end if;
      return 0;
   end Middle;
begin
   if Total > 0 and then Total < 100 then
      Total := Middle (Total);
   end if;
end Outer;
