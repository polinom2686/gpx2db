#include <iostream>
#include <fstream>
#include <string>
#include <memory>
#include <ctime>

#include "nanodbc.h"

using namespace std;

// <hdop>3.4</hdop><vdop>2.9</vdop><sat>13</sat><fix>3d</fix></trkpt>

const string XML_LATbeg = "lat=\"";
const string XML_LATend = "\"";
const string XML_LONbeg = "lon=\"";
const string XML_LONend = "\"";
const string XML_ELEbeg = "<ele>";
const string XML_ELEend = "</ele>";
const string XML_TIMEbeg = "<time>";
const string XML_TIMEend = "</time>";
const string XML_HDOPbeg = "<hdop>";
const string XML_HDOPend = "</hdop>";
const string XML_VDOPbeg = "<vdop>";
const string XML_VDOPend = "</vdop>";
const string XML_SATbeg = "<sat>";
const string XML_SATend = "</sat>";
const string XML_FIXbeg = "<fix>";
const string XML_FIXend = "</fix>";

struct gpsdata
{
    string   lat;
    string   lon;
    string   ele;
    string   utc;
    string   hdop;
    string   vdop;
    string   sat;
    string   fix;
};

string getParam(string& sdata, string sbeg, string send)
{
    size_t pbeg = 0;
    size_t pend = 0;

    pbeg = sdata.find(sbeg);
    if (pbeg == string::npos)
    {
        return "";
    }

    pbeg += sbeg.size();
    pend = sdata.find(send, pbeg);

    string temp = sdata.substr(pbeg, pend - pbeg);

    return temp;
}

/*
  returns the utc timezone offset
  (e.g. -8 hours for PST)
*/
int get_utc_offset() {

  time_t zero = 24*60*60L;
  struct tm * timeptr;
  int gmtime_hours;

  /* get the local time for Jan 2, 1900 00:00 UTC */
  timeptr = localtime( &zero );
  gmtime_hours = timeptr->tm_hour;

  /* if the local time is the "day before" the UTC, subtract 24 hours
    from the hours to get the UTC offset */
  if( timeptr->tm_mday < 2 )
    gmtime_hours -= 24;

  return gmtime_hours;

}

/*
  the utc analogue of mktime,
  (much like timegm on some systems)
*/
time_t tm_to_time_t_utc( struct tm * timeptr ) {

  /* gets the epoch time relative to the local time zone,
  and then adds the appropriate number of seconds to make it UTC */
  return mktime( timeptr ) + get_utc_offset() * 3600;

}

string query_insert(gpsdata& data)
{
    string temp;
    
    temp += "insert into points (UTC, LAT, LON, ELE, SAT, HDOP, VDOP, FIX) ";
    temp += "values (";
    temp += data.utc + ", " + data.lat + ", " + data.lon + ", " + data.ele + ", " +
            data.sat + ", " + data.hdop + ", " + data.vdop + ", '" + data.fix + "');";
    
    return temp;
}

int main(int argc, char* argv[])
{
    // gpx2db path_file dsn_name dsn_user dsn_pass

    if (argc != 5)
    {
        cout << "running: gpx2db path_file dsn_name dsn_user dsn_pass\n";
        return 1;
    }

    string filePath(argv[1]);
    string dsnName(argv[2]);
    string dsnUser(argv[3]);
    string dsnPass(argv[4]);

    size_t fileSize;
    fstream fs;

    fs.open(filePath);
    if (!fs.is_open())
    {
        cout << "file not openend\n";
        return 2;
    }

    fs.seekg(0,ios::end);
    fileSize = fs.tellg();
    fs.seekg(0,ios::beg);

    if (0 == fileSize)
    {
        cout << "file is empty\n";
        return 3;
    }

    unique_ptr<char[]> fileData;
    try
    {
        fileData = unique_ptr<char[]>(new char[fileSize]);
    }
    catch(bad_alloc)
    {
        cout << "error: not allocated memory\n";
        return 4;
    }

    string sdata;
    vector<gpsdata> gps;
    gps.clear();

    while (getline(fs,sdata))
    {
        gpsdata dtemp;
        string  stemp;

        // lat lon ele time hdop vdop sat fix

        stemp = getParam(sdata,XML_LATbeg,XML_LATend);
        if (0 == stemp.size())
        {
            continue;
        }
        dtemp.lat = stemp;
        dtemp.lon = getParam(sdata,XML_LONbeg,XML_LONend);
        dtemp.ele = getParam(sdata,XML_ELEbeg,XML_ELEend);
        stemp = getParam(sdata,XML_TIMEbeg,XML_TIMEend);
        struct tm t;
        t.tm_year = stoi(stemp.substr(0,4)) - 1900;
        t.tm_mon = stoi(stemp.substr(5,2)) - 1;
        t.tm_mday = stoi(stemp.substr(8,2));
        t.tm_hour = stoi(stemp.substr(11,2));
        t.tm_min = stoi(stemp.substr(14,2));
        t.tm_sec = stoi(stemp.substr(17,2));
        dtemp.utc = to_string(tm_to_time_t_utc(&t));
        dtemp.hdop = getParam(sdata,XML_HDOPbeg,XML_HDOPend);
        dtemp.vdop = getParam(sdata,XML_VDOPbeg,XML_VDOPend);
        dtemp.sat = getParam(sdata,XML_SATbeg,XML_SATend);
        dtemp.fix = getParam(sdata,XML_FIXbeg,XML_FIXend);

        gps.push_back(dtemp);
    }
    
    if (gps.empty())
    {
        return 5;
    }

    #ifdef DEBUG
    if (gps.size() > 0)
    {
        fstream fsd("debug.info", ios::out);
        for (size_t i = 0; i < gps.size(); ++i)
        {
            // lat lon ele time hdop vdop sat fix
            fsd << gps[i].lat << "\t"
                << gps[i].lon << "\t"
                << gps[i].ele << "\t"
                << gps[i].utc << "\t"
                << gps[i].hdop << "\t"
                << gps[i].vdop << "\t"
                << gps[i].sat << "\t"
                << gps[i].fix << endl;
            fsd << query_insert(gps[i]) << endl;
        }
        fsd.close();
        return 0;
    }
    #endif // DEBUG
    
    /* Insert data to DB
    */

    nanodbc::connection conn;
    
    try
    {
        conn.connect(dsnName,dsnUser,dsnPass);
    }
    catch(nanodbc::database_error dbe)
    {
        cout << dbe.what() << endl;
    }
    
    for (size_t i = 0; i < gps.size(); ++i)
    {
        string qtemp;
        
        qtemp = query_insert(gps[i]);
        
        nanodbc::just_execute(conn, qtemp);
    }
    
    conn.disconnect();

    return 0;
}
