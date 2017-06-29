import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;

public class gp_conn {

    public static void main(String[] args) {
        try {
            Class.forName("org.postgresql.Driver");
            Connection db = DriverManager.getConnection("jdbc:postgresql://mygpdbpub.gpdb.rds.aliyuncs.com:3568/postgres","mygpdb","mygpdb");

            Statement st = db.createStatement();
            ResultSet rs = st.executeQuery("select * from gp_segment_configuration;");
            while (rs.next()) {
                System.out.print(rs.getString(1));
                System.out.print("    |    ");  
                System.out.print(rs.getString(2));  
                System.out.print("    |    ");  
                System.out.print(rs.getString(3));  
                System.out.print("    |    ");  
                System.out.print(rs.getString(4));  
                System.out.print("    |    ");  
                System.out.print(rs.getString(5));  
                System.out.print("    |    ");  
                System.out.print(rs.getString(6));  
                System.out.print("    |    ");  
                System.out.print(rs.getString(7));  
                System.out.print("    |    ");  
                System.out.print(rs.getString(8));  
                System.out.print("    |    ");  
                System.out.print(rs.getString(9));  
                System.out.print("    |    ");  
                System.out.print(rs.getString(10));  
                System.out.print("    |    ");  
                System.out.println(rs.getString(11));  
            }  
            rs.close();  
            st.close();  
        } catch (ClassNotFoundException e) {  
            e.printStackTrace();  
        } catch (SQLException e) {  
            e.printStackTrace();  
        }  
    }  
}
