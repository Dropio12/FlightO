//
// Created by Mario Marchand
//

#include "DonneesGTFS.h"
using namespace std;

const std::string readFile(const std::string &p_nomFichier){
    std::ifstream* file = new std::ifstream();
    file->open(p_nomFichier); //Lire le fichier
    std::string str;
    std::ostringstream stream;
    stream << file->rdbuf();
    str = stream.str(); //Obtenir la string du fichier
    if(str.length()==0){
        throw logic_error("Erreur de la lecture du fichier "+p_nomFichier); //erreur
    }
    delete file;
    return str;
}

const CategorieBus getCategory(const std::string &color){

}

//! \brief ajoute les lignes dans l'objet GTFS
//! \param[in] p_nomFichier: le nom du fichier contenant les lignes
//! \throws logic_error si un problème survient avec la lecture du fichier
void DonneesGTFS::ajouterLignes(const std::string &p_nomFichier)
{
    //écrire votre code ici
    std::string str = readFile(p_nomFichier);
    std::vector<std::string> entries = this->string_to_vector(str, '\n');
    for(int i=1; i<entries.size();i++){
        std::vector<std::string> data = this->string_to_vector(entries[i],',');
        Ligne ligne (data.at(0), data.at(2), data.at(4), Ligne::couleurToCategorie(data.at(7)));
        std::pair<std::string, Ligne> id (data[0], ligne);
        m_lignes.insert(id);
        m_lignes_par_numero.insert(std::pair<string, Ligne>(data.at(2), ligne));
    }
}

//! \brief ajoute les stations dans l'objet GTFS
//! \param[in] p_nomFichier: le nom du fichier contenant les stations
//! \throws logic_error si un problème survient avec la lecture du fichier
void DonneesGTFS::ajouterStations(const std::string &p_nomFichier)
{
    //écrire votre code ici
    std::string str = readFile(p_nomFichier);
    std::vector<std::string> entries = this->string_to_vector(str, '\n');
    for(int i=1; i<entries.size();i++){
        std::vector<std::string> data = this->string_to_vector(entries[i],',');
        std::pair<std::string, Station> station (data[0], Station(data.at(0), data.at(2), data.at(3), Coordonnees(std::stod(data.at(4)), std::stod(data.at(5)))));
        m_stations.insert(station);
    }
}

//! \brief ajoute les transferts dans l'objet GTFS
//! \breif Cette méthode doit âtre utilisée uniquement après que tous les arrêts ont été ajoutés
//! \brief les transferts (entre stations) ajoutés sont uniquement ceux pour lesquelles les stations sont prensentes dans l'objet GTFS
//! \brief les transferts sont ajoutés dans m_transferts
//! \brief les from_station_id des stations de m_transferts sont ajoutés dans m_stationsDeTransfert
//! \param[in] p_nomFichier: le nom du fichier contenant les station
//! \throws logic_error si un problème survient avec la lecture du fichier
//! \throws logic_error si tous les arrets de la date et de l'intervalle n'ont pas été ajoutés
void DonneesGTFS::ajouterTransferts(const std::string &p_nomFichier)
{
    //écrire votre code ici
    if(!m_tousLesArretsPresents){
        throw logic_error("Les arrets ne sont pas tous ajoutes");
    }
    std::string str = readFile(p_nomFichier);
    std::vector<std::string> entries = this->string_to_vector(str, '\n');
    for(int i=1; i<entries.size();i++){
        std::vector<std::string> data = this->string_to_vector(entries[i],',');
        if(m_stations.find(data.at(0))!= m_stations.end() && m_stations.find(data.at(1))!= m_stations.end()){
            unsigned int time = std::stoi(data.at(3));
            if(std::stoi(data.at(3))==0){
                time= 1;
            }
            std::tuple<std::string, std::string, unsigned int> transfert (data.at(0), data.at(1), time);
            m_transferts.insert(m_transferts.end(), transfert);
            m_stationsDeTransfert.insert(data.at(0));
        }
    }
}


//! \brief ajoute les services de la date du GTFS (m_date)
//! \param[in] p_nomFichier: le nom du fichier contenant les services
//! \throws logic_error si un problème survient avec la lecture du fichier
void DonneesGTFS::ajouterServices(const std::string &p_nomFichier)
{
    //écrire votre code ici
    std::string str = readFile(p_nomFichier);
    std::vector<std::string> entries = this->string_to_vector(str, '\n');
    for(int i=1; i<entries.size();i++){
        std::vector<std::string> data = this->string_to_vector(entries[i],',');
        if(data.at(2)=="1"){
            if(this->m_date== Date(std::stoi(data.at(1).substr(0,4)),std::stoi(data.at(1).substr(4,2)),std::stoi(data.at(1).substr(6,2)))){
                m_services.insert(data.at(0));
            }
        }
    }
}

//! \brief ajoute les voyages de la date
//! \brief seuls les voyages dont le service est présent dans l'objet GTFS sont ajoutés
//! \param[in] p_nomFichier: le nom du fichier contenant les voyages
//! \throws logic_error si un problème survient avec la lecture du fichier
void DonneesGTFS::ajouterVoyagesDeLaDate(const std::string &p_nomFichier)
{
    //écrire votre code ici
    std::string str = readFile(p_nomFichier);
    std::vector<std::string> entries = this->string_to_vector(str, '\n');
    for(int i=1; i<entries.size();i++){
        std::vector<std::string> data = this->string_to_vector(entries[i],',');
        if(m_services.find(data.at(1))!= m_services.end()){
            std::pair<std::string, Voyage> voyage (data.at(3), Voyage(data.at(3), data.at(0), data.at(1), data.at(4)));
            m_voyages.insert(voyage);
        }
    }
}

//! \brief ajoute les arrets aux voyages présents dans le GTFS si l'heure du voyage appartient à l'intervalle de temps du GTFS
//! \brief Un arrêt est ajouté SSI son heure de départ est >= now1 et que son heure d'arrivée est < now2
//! \brief De plus, on enlève les voyages qui n'ont pas d'arrêts dans l'intervalle de temps du GTFS
//! \brief De plus, on enlève les stations qui n'ont pas d'arrets dans l'intervalle de temps du GTFS
//! \param[in] p_nomFichier: le nom du fichier contenant les arrets
//! \post assigne m_tousLesArretsPresents à true
//! \throws logic_error si un problème survient avec la lecture du fichier
void DonneesGTFS::ajouterArretsDesVoyagesDeLaDate(const std::string &p_nomFichier)
{
    //écrire votre code ici
    //écrire votre code ici
    std::string str = readFile(p_nomFichier);
    std::vector<std::string> entries = this->string_to_vector(str, '\n');
    for(int i=1; i<entries.size();i++){
        std::vector<std::string> data = this->string_to_vector(entries[i],',');
        Heure arrival (std::stoi(data.at(1).substr(0, 2)), std::stoi(data.at(1).substr(3, 2)), std::stoi(data.at(1).substr(6, 2)));
        Heure departure (std::stoi(data.at(2).substr(0, 2)), std::stoi(data.at(2).substr(3, 2)), std::stoi(data.at(2).substr(6, 2)));
        //Ajouter les arrets SSI ils sont entre les 2h
        if( this->m_now1 <= departure && this->m_now2>arrival){
            std::map<std::string, Voyage>::iterator itVoyage = m_voyages.find(data.at(0));
            std::map<std::string, Station>::iterator itStation = m_stations.find(data.at(3));
            shared_ptr<Arret> ar = make_shared<Arret>(data.at(3),arrival, departure,std::stoi(data.at(4)), data.at(0));
            if(itStation != m_stations.end() && itVoyage != m_voyages.end()){

                itVoyage->second.ajouterArret(ar);
                itStation->second.addArret(ar);
                m_nbArrets++;
            }
        }
    }
    //filtrer les voyages et les stations
    std::map<std::string, Voyage>::iterator itVoyage = m_voyages.begin();
    while(itVoyage != m_voyages.end()){
        if(itVoyage->second.getNbArrets()==0){
            std::map<std::string, Voyage>::iterator it = itVoyage;
            itVoyage++;
            m_voyages.erase(it);
        }else{
            itVoyage++;
        }
    }
    std::map<std::string, Station>::iterator itStation = m_stations.begin();
    while(itStation != m_stations.end()){
        if(itStation->second.getNbArrets()==0){
            std::map<std::string, Station>::iterator it = itStation;
            itStation++;
            m_stations.erase(it);
        }else{
            itStation++;
        }
    }
    this->m_tousLesArretsPresents = true;
}



