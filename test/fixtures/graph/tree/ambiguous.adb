package body Ambiguous is
   Total : Integer := 0;
   Table : array (1 .. 4) of Integer;

   function Scale (X : Integer) return Integer is
   begin
      return X + 1;
   end Scale;

   procedure Drive is
   begin
      Total := Scale (1);
      Total := Table (2);
   end Drive;
end Ambiguous;
