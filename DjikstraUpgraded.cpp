//
// Created by Mario Marchand on 16-12-30.
//

#include "ReseauGTFS.h"
#include <sys/time.h>

using namespace std;

//! \brief ajout des arcs dus aux voyages
//! \brief insère les arrêts (associés aux sommets) dans m_arretDuSommet et m_sommetDeArret
//! \throws logic_error si une incohérence est détecté lors de cette étape de construction du graphe
void ReseauGTFS::ajouterArcsVoyages(const DonneesGTFS & p_gtfs)
{
	//écrire votre code ici
    map<basic_string<char>,Voyage> voyages = p_gtfs.getVoyages();
    int i = m_arretDuSommet.size();
    for(pair<basic_string<char>, Voyage> p : voyages){
        m_arretDuSommet.resize(m_arretDuSommet.size()+p.second.getNbArrets()); //Ajouter x espace
        shared_ptr<Arret> last=nullptr;
        for(const shared_ptr<Arret> a : p.second.getArrets()){
            m_arretDuSommet[i] = a;
            m_sommetDeArret.insert(std::pair<shared_ptr<Arret>, ::size_t>(a,i)) ;
            if(last!= nullptr){
                size_t cost = a->getHeureArrivee()- last->getHeureArrivee();
                m_leGraphe.ajouterArc(m_sommetDeArret[last], i, cost);
            }
            last = a;
            i++;
        }
    }
}


//A reverifier
//! \brief ajouts des arcs dus aux transferts entre stations
//! \throws logic_errSor si une incohérence est détecté lors de cette étape de construction du graphe
void ReseauGTFS::ajouterArcsTransferts(const DonneesGTFS & p_gtfs)
{
	//écrire votre code ici
    map<std::string, Station> stations= p_gtfs.getStations();
    map<std::string, Voyage> voyages=p_gtfs.getVoyages();
    unordered_map<std::string, Ligne> lignes =p_gtfs.getLignes();


    for(std::tuple<std::string, std::string, unsigned int> transfert:  p_gtfs.getTransferts()){
        Station firstStation = stations[std::get<0>(transfert)];
        Station secondStation = stations[std::get<1>(transfert)];

        for(const pair<Heure, shared_ptr<Arret>> firstStop: firstStation.getArrets()){

            unordered_map<std::string, shared_ptr<Arret>> corr = unordered_map<std::string, shared_ptr<Arret>>();
            Heure heureMinimale = firstStop.second->getHeureArrivee().add_secondes(std::get<2>(transfert));
            auto it =secondStation.getArrets().lower_bound(heureMinimale);

            while(it!=secondStation.getArrets().end()){
                pair<Heure, shared_ptr<Arret>> secondStop = *it;
                Ligne firstLine = lignes[voyages[firstStop.second->getVoyageId()].getLigne()];
                Ligne secondLine = lignes[voyages[secondStop.second->getVoyageId()].getLigne()];

                if(firstLine.getNumero()!=secondLine.getNumero()) { //Verifier qu on a pas 2 bus avec le meme numero
                    if(corr.find(secondLine.getNumero()) !=corr.end()){
                        shared_ptr<Arret> someClosestStop = corr.at(secondLine.getNumero());

                        if(someClosestStop->getHeureArrivee()> secondStop.second->getHeureArrivee()){
                            m_leGraphe.enleverArc(m_sommetDeArret[firstStop.second],
                                                  m_sommetDeArret[someClosestStop]);
                            m_leGraphe.ajouterArc(m_sommetDeArret[firstStop.second],
                                                  m_sommetDeArret[secondStop.second],
                                                  secondStop.second->getHeureArrivee()- firstStop.second->getHeureArrivee());
                            corr.insert(pair<std::string, shared_ptr<Arret>>(secondLine.getNumero(), secondStop.second));
                        }
                    }else{
                        m_leGraphe.ajouterArc(m_sommetDeArret[firstStop.second],
                                              m_sommetDeArret[secondStop.second],
                                              secondStop.second->getHeureArrivee()- firstStop.second->getHeureArrivee());
                        corr.insert(pair<std::string, shared_ptr<Arret>>(secondLine.getNumero(), secondStop.second));
                    }
                }
                it++;
            }
        }
    }
}

//! \brief ajouts des arcs d'une station à elle-même pour les stations qui ne sont pas dans DonneesGTFS::m_stationsDeTransfert
//! \throws logic_error si une incohérence est détecté lors de cette étape de construction du graphe
void ReseauGTFS::ajouterArcsAttente(const DonneesGTFS & p_gtfs)
{
	//écrire votre code ici
    map<std::string, Station> stations= p_gtfs.getStations();
    map<std::string, Voyage> voyages = p_gtfs.getVoyages();
    unordered_map<std::string, Ligne> lignes = p_gtfs.getLignes();

    unordered_set<std::string> transferableStations = unordered_set<std::string>();
    for(std::tuple<std::string, std::string, unsigned int> transfert:  p_gtfs.getTransferts()){ //Verifier si c
        transferableStations.insert(std::get<0>(transfert));
    }

    list<Station> nonTransferableStations = list<Station>();
    for(pair<std::string, Station> station: stations){
        if(transferableStations.find(station.second.getId())==transferableStations.end()){
            nonTransferableStations.push_back(station.second);
        }
    }

    for(Station station: nonTransferableStations){
        list<std::string> lines = list<std::string>();
        for(const pair<Heure, shared_ptr<Arret>> arret : station.getArrets()){
            Voyage voyage = voyages[arret.second->getVoyageId()];
            if(std::find(lines.begin(), lines.end(),voyage.getLigne())== lines.end()){
                lines.push_back(voyage.getLigne());
            }
        }
        if(lines.size()>1){
            for(const pair<Heure, shared_ptr<Arret>> firstStop : station.getArrets()){
                unordered_map<std::string, shared_ptr<Arret>> corr = unordered_map<std::string, shared_ptr<Arret>>();
                auto it =station.getArrets().lower_bound(firstStop.second->getHeureArrivee().add_secondes(ReseauGTFS::delaisMinArcsAttente));

                while(it!=station.getArrets().end()){
                    pair<Heure, shared_ptr<Arret>> secondStop = *it;
                    Voyage firstTrip = voyages[firstStop.second->getVoyageId()];
                    Voyage secondTrip = voyages[secondStop.second->getVoyageId()];
                    if(lignes[firstTrip.getLigne()].getNumero() !=  lignes[secondTrip.getLigne()].getNumero()){
                        if(corr.find(lignes[secondTrip.getLigne()].getNumero()) !=corr.end()){
                            shared_ptr<Arret> someClosestStop = corr.at(lignes[secondTrip.getLigne()].getNumero());
                            if(someClosestStop->getHeureArrivee()> secondStop.second->getHeureArrivee()){
                                m_leGraphe.enleverArc(m_sommetDeArret[firstStop.second],
                                                      m_sommetDeArret[someClosestStop]);
                                m_leGraphe.ajouterArc(m_sommetDeArret[firstStop.second],
                                                      m_sommetDeArret[secondStop.second],
                                                      secondStop.second->getHeureArrivee()- firstStop.second->getHeureArrivee());
                                corr.insert(pair<std::string, shared_ptr<Arret>>(lignes[secondTrip.getLigne()].getNumero(), secondStop.second));
                            }
                        }else{
                            m_leGraphe.ajouterArc(m_sommetDeArret[firstStop.second],
                                                  m_sommetDeArret[secondStop.second],
                                                  secondStop.second->getHeureArrivee()- firstStop.second->getHeureArrivee());
                            corr.insert(pair<std::string, shared_ptr<Arret>>(lignes[secondTrip.getLigne()].getNumero(), secondStop.second));
                        }

                    }
                    ++it;
                }
            }
        }
    }
}


//! \brief ajoute des arcs au réseau GTFS à partir des données GTFS
//! \brief Il s'agit des arcs allant du point origine vers une station si celle-ci est accessible à pieds et des arcs allant d'une station vers le point destination
//! \param[in] p_gtfs: un objet DonneesGTFS
//! \param[in] p_pointOrigine: les coordonnées GPS du point origine
//! \param[in] p_pointDestination: les coordonnées GPS du point destination
//! \throws logic_error si une incohérence est détecté lors de la construction du graphe
//! \post constuit un réseau GTFS représenté par un graphe orienté pondéré avec poids non négatifs
//! \post assigne la variable m_origine_dest_ajoute à true (car les points orignine et destination font parti du graphe)
//! \post insère dans m_sommetsVersDestination les numéros de sommets connctés au point destination
void ReseauGTFS::ajouterArcsOrigineDestination(const DonneesGTFS &p_gtfs, const Coordonnees &p_pointOrigine,
                                               const Coordonnees &p_pointDestination)
{
	//écrire votre code ici
    Arret origin = Arret(stationIdOrigine, Heure(0,0,0), Heure(0,0,1), 0, "v");
    Arret destination = Arret(stationIdDestination, Heure(0,0,0), Heure(0,0,1), 0, "v");
    shared_ptr<Arret> oShared = make_shared<Arret>(origin);
    shared_ptr<Arret> dShared = make_shared<Arret>(destination);

    m_arretDuSommet.resize(m_arretDuSommet.size()+2);
    m_leGraphe.resize(m_leGraphe.getNbSommets()+2);
    m_arretDuSommet[m_arretDuSommet.size()-2] = oShared;
    m_arretDuSommet[m_arretDuSommet.size()-1] = dShared;
    m_sommetDeArret.insert(pair<shared_ptr<Arret>, size_t>(oShared, m_arretDuSommet.size()-2));
    m_sommetDeArret.insert(pair<shared_ptr<Arret>, size_t>(dShared, m_arretDuSommet.size()-1));
    m_sommetOrigine = m_arretDuSommet.size()-2;
    m_sommetDestination = m_arretDuSommet.size()-1;

    map<std::string, Voyage> voyages = p_gtfs.getVoyages();
    unordered_map<std::string, Ligne> lignes = p_gtfs.getLignes();



    m_nbArcsStationsVersDestination=0;  //Il y a un bug je (pas dans mon code) qui le met a 0 au debut.

    for(pair<std::string, Station> station: p_gtfs.getStations()){
        double distanceOrigin = p_pointOrigine-station.second.getCoords();
        double distanceDestination = p_pointDestination-station.second.getCoords();


        if(distanceOrigin<= distanceMaxMarche){
            double tempsMarche = (distanceOrigin/vitesseDeMarche)*60*60;
            Heure heureArrive = p_gtfs.getTempsDebut().add_secondes(tempsMarche);
            unordered_map<std::string, shared_ptr<Arret>> corr = unordered_map<std::string, shared_ptr<Arret>>();

            auto it =station.second.getArrets().lower_bound(heureArrive);

            while(it!=station.second.getArrets().end()){
                pair<Heure, shared_ptr<Arret>> arret = *it;
                Ligne stopLigne = lignes[voyages[arret.second->getVoyageId()].getLigne()];
                if(corr.find(stopLigne.getNumero()) !=corr.end()){
                    shared_ptr<Arret> someClosestStop = corr.at(stopLigne.getNumero());
                    if(someClosestStop->getHeureArrivee()> arret.second->getHeureArrivee()){
                        m_leGraphe.enleverArc(m_sommetDeArret[oShared],
                                              m_sommetDeArret[someClosestStop]);
                        m_leGraphe.ajouterArc(m_sommetDeArret[oShared],
                                              m_sommetDeArret[arret.second],
                                              arret.second->getHeureArrivee() -  p_gtfs.getTempsDebut());
                        corr.insert(pair<std::string, shared_ptr<Arret>>(stopLigne.getNumero(), arret.second));
                    }
                }else{
                    m_leGraphe.ajouterArc(m_sommetDeArret[oShared],
                                          m_sommetDeArret[arret.second],
                                          arret.second->getHeureArrivee() -  p_gtfs.getTempsDebut());
                    corr.insert(pair<std::string, shared_ptr<Arret>>(stopLigne.getNumero(), arret.second));
                    m_nbArcsOrigineVersStations++;
                }
                ++it;
            }
        }
        if(distanceDestination<= distanceMaxMarche){
            double tempsMarche = (distanceDestination/vitesseDeMarche)*60*60;
            int i = m_sommetsVersDestination.size();
            m_sommetsVersDestination.resize(m_sommetsVersDestination.size()+station.second.getNbArrets());
            for(pair<const Heure, shared_ptr<Arret>> arret: station.second.getArrets()){
                m_leGraphe.ajouterArc(m_sommetDeArret[arret.second],
                                      m_sommetDeArret[dShared],
                                      tempsMarche);
                m_sommetsVersDestination[i] = m_sommetDeArret[arret.second];
                i++;
                m_nbArcsStationsVersDestination++;
            }
        }
    }
    m_origine_dest_ajoute=true;
}

//! \brief Remet ReseauGTFS dans l'était qu'il était avant l'exécution de ReseauGTFS::ajouterArcsOrigineDestination()
//! \param[in] p_gtfs: un objet DonneesGTFS
//! \throws logic_error si une incohérence est détecté lors de la modification du graphe
//! \post Enlève de ReaseauGTFS tous les arcs allant du point source vers un arrêt de station et ceux allant d'un arrêt de station vers la destination
//! \post assigne la variable m_origine_dest_ajoute à false (les points orignine et destination sont enlevés du graphe)
//! \post enlève les données de m_sommetsVersDestination
void ReseauGTFS::enleverArcsOrigineDestination()
{
	//écrire votre code ici
    for(size_t sommet: m_sommetsVersDestination){
        m_leGraphe.enleverArc(sommet, m_sommetDestination);
        m_nbArcsStationsVersDestination--;
    }

    m_leGraphe.resize(m_leGraphe.getNbSommets()-2);
    m_nbArcsOrigineVersStations=0;

    std::remove(m_arretDuSommet.begin(), m_arretDuSommet.end(),m_arretDuSommet[m_sommetOrigine]);
    std::remove(m_arretDuSommet.begin(), m_arretDuSommet.end(),m_arretDuSommet[m_sommetDestination]);

    m_arretDuSommet.resize(m_arretDuSommet.size()-2);

    m_origine_dest_ajoute = false;
}


