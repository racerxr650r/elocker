--  categories.adb — one instance of each ELOC category, and one of each
--  exclusion. Hand-counted in README.md beside this file.

with Ada.Text_IO;

procedure Categories (N : Integer) is
   Limit : constant Integer := 3;
   Bare  : Integer;
   Total : Integer := 0;
begin
   for I in 1 .. Limit loop
      if I = N then
         Total := Total + I;
      elsif I > N then
         exit;
      else
         null;
      end if;
   end loop;

   while Total > Limit loop
      Total := Total - 1;
   end loop;

   case N is
      when 0 =>
         Total := 0;
      when others =>
         Total := 1;
   end case;

   if Total > 0 and then Total < 100 then
      Ada.Text_IO.Put_Line ("ok");
   end if;

   begin
      raise Constraint_Error;
   exception
      when Constraint_Error =>
         Total := 0;
   end;
end Categories;
